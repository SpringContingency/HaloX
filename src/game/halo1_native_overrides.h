#pragma once

// halo1.dll crashes shortly after initialize_game returns with a
// STATUS_INTEGER_DIVIDE_BY_ZERO at halo1.dll+0x1FCDD9. The faulting function,
// FUN_1801fcd40, is the depth-mip buffer setup; it receives (width, height) in
// (rcx, rdx) but its caller fetches them from a render-config global that
// hasn't been populated by our launcher path. The result: width=height=0, an
// IDIV underflow, and the game thread dies.
//
// Workaround: detour FUN_1801fcd40 (RVA 0x1FCD40) and substitute non-zero
// defaults when the args come in as zero. We use the rasterizer swap-chain's
// back-buffer dimensions when available, falling back to 1280x720.
//
// Install once per halo1 launch, BEFORE initialize_game runs. Idempotent.
void halo1_install_div_zero_fix();

// halo1's CreateGameEngine internals call FUN_1800ae410 (RVA 0xAE410) which
// dereferences a render-config struct pointer at *(halo1+0x2E3C090 + 0x118).
// Our launcher path used to skip the master init step (PreloadCommonBegin /
// vtable slot 4), so that field stayed null and the loop body inside
// FUN_1800ae410 read width/height from a NULL pointer (AV at halo1+0xAE635).
//
// The proper fix is to call PreloadCommonBegin (i_game_engine vtable slot 4)
// from the launcher — Blam-Creation-Suite's MCC launcher does exactly this
// before InitGraphics. Halo3+ stubbed slot 4 to a no-op, but halo1's slot 4
// still does the real preload work. See game_instance_manager.launch.cpp.
//
// This stub is kept as a defensive belt-and-suspenders measure: if a future
// engine code path zeros the field, the stub keeps the four width/height
// slots populated so reads return sane non-zero values instead of AV'ing.
// It is not the primary fix any more — the launcher-side init call is.
//
// Called from launch_game_internal BEFORE create_game_engine. Idempotent.
void halo1_install_render_config_stub();

// Stamp halo1's DAT_182e3bdd8 (the c_engine_context pointer) with a halox-built
// replica of the structure, snooped live from MCC's running halo1 on
// 2026-04-29. Defensive: only stamps if the global is currently NULL or its
// existing descriptor's height field reads as the 0xFFFFFFFF "uninitialized"
// sentinel — never overwrites a fully-populated context that slot 1
// successfully built. See halo1_native_overrides.cpp for the layout details.
//
// Call AFTER game_engine->initialize (slot 1 has had its chance to populate
// the global on its own) and BEFORE initialize_game (which spawns the game
// thread that touches FUN_1801fcd40). Idempotent.
void halo1_install_engine_ctx_stub();

// Call halo1.dll's i_game_engine vtable slot 4 — PreloadCommonBegin(device).
// In Blam-Creation-Suite's MCC launcher this is invoked between
// create_game_engine and InitGraphics; halo1's implementation allocates the
// engine-wide preload state struct at DAT_182e3d2f8 and reads game.cfg
// (Preload.usePaks / Preload.usePreloadUui), which a large amount of later
// engine code (including PlayGame's leading branch) is gated on. Halo3+
// stubbed this slot to a no-op, which is why our launcher path got away with
// skipping it for every other module.
//
// Implemented by reading the engine's vtable[4] and calling it directly with
// (this, device). Does NOT install any hook — pure thunked virtual call.
void halo1_call_preload_common_begin(void* engine, void* device);

// Call halo1.dll's i_game_engine vtable slot 5 — PreloadLevelBegin(map_index).
// Map index is halo1-internal (NOT the libmcc map_id enum) and indexes into
// PTR_DAT_18188b3f0. Pass -1 to skip the per-map preload (slot 5 no-ops on
// negative indices). Sets a "preload-done" flag at DAT_182e3d2f8 + 0xcd01a8
// that PlayGame's first if-block reads.
//
// Pure virtual call, no hooks.
void halo1_call_preload_level_begin(void* engine, int map_index);

// Run halo1's "SYS::haloInit" engine-subsystem initializer by directly calling
// FUN_180AC2528 (RVA 0xAC2528). This is the wrapper that allocates the
// 0x1F10-byte engine-state block at DAT_182D91330 and runs Session command
// handler installation + ~30 subsystem inits. Without this, the game thread
// halts at halo1+0xAD7D58 with code 0xBEEF0117 because the precondition
// `*DAT_182D91330 != 0` is never satisfied. MCC drives this via a tag-name
// dispatcher that fires "SYS::haloInit"; halox calls the underlying function
// directly. One-shot guarded internally.
//
// Call BEFORE game_engine->initialize_game (which is what flips DAT_182E3B9A0
// to non-zero and sets up the halt condition).
void halo1_call_engine_subsystem_init();

// --- Heap-only vtable swap on RT instances ---
//
// halo1's FUN_1800AE410 (InitializeRenderTargets) does:
//   plVar3 = (RT->vt[0x1A])(RT, 0);   // GetSubResource — returns NULL on
//                                     // partially-init RTs in our launcher
//                                     // path
//   AddRef(plVar3)                    // AVs at AE635 because plVar3==NULL
//
// To stop the AV without modifying any halo1.dll code or .rdata, we walk the
// RT manager's table (DAT_181bea6b8 + 0x1B8) and rewrite each RT instance's
// first 8 bytes (its vtable pointer) to point to a heap-allocated copy of
// the original vtable with two slots overridden:
//   slot 0x1A (offset 0xD0) → stub_get_subresource — returns &g_stub_subresource
//   slot 0x1B (offset 0xD8) → stub_get_mip_subresource — same
// g_stub_subresource is a heap object whose own vtable's slot 1 (AddRef) and
// slot 2 (Release) are no-ops. The downstream AddRef call lands on our stub
// instead of NULL.
//
// Idempotent: skips entries whose vtable pointer is already one of our
// known copies. Safe to call repeatedly from a poller.
//
// Returns the number of entries swapped on this call. Returns -1 if the RT
// manager is not yet allocated (DAT_181bea6b8 == 0).
int halo1_swap_rt_vtables_once();

// Spawn a worker thread that calls halo1_swap_rt_vtables_once() every 25ms
// for up to 5s. Run alongside the game-init thread to catch RT entries the
// instant they appear in the manager. Idempotent — repeated calls are no-ops.
void halo1_start_rt_vtable_swap_poller();

// Create the real ID3D11Texture2D + SRV that the RT vtable swap stubs return.
// Halo1's normal init populates its RT subresource tables with real d3d11
// resource pointers; our stubs need to return the same KIND of object (a real
// d3d11 COM object, not a fake C++ stub) or d3d11's command processor crashes
// deep in its internals when it tries to walk the SRV. Idempotent. Must be
// called BEFORE halo1_start_rt_vtable_swap_poller() so the stubs have a real
// resource to hand back.
struct ID3D11Device;
void halo1_init_stub_d3d11_resource(ID3D11Device* device);

// Runtime byte-patch in halo1.dll's loaded image: NOP out the
// `mov rdx, [rax]; call [rdx+8]` pair at halo1+0xAE635..0xAE63A. The CALL is
// the AddRef on the result of the prior `vt[0xD0](this, 0)` GetSubResource
// at halo1+0xAE624, which returns NULL on partially-init RTs in our launcher
// path. With the result stored as NULL in DAT_181b85e80 and the AddRef
// skipped, the rest of InitializeRenderTargets proceeds without AV'ing —
// confirmed empirically (user verified the same patch works in MCC).
//
// This is a process-private RWX modification of the loaded DLL image: no
// on-disk change, no MinHook/detour, no inline trampoline — just 6 bytes of
// 0x90 written via VirtualProtect. Idempotent; safe across re-launches.
//
// SUPERSEDED 2026-04-28 by halo1_install_init_render_targets_spinwait —
// the underlying problem turned out to be a thread race, not a permanently
// NULL pool pointer. The NOP masked the race by making the AddRef-on-NULL
// a no-op, which let the engine continue in a half-broken state and hit
// further crashes downstream. Kept here for fallback/diagnostic use only.
void halo1_install_rt_subresource_addref_nop();

// Detour FUN_1800AE410 (halo1's InitializeRenderTargets) entry to spin-wait
// for the engine main RT root's pool pointer (+0xe8 of *(DAT_182e3c090 +0x500))
// to become non-NULL before delegating to the original. The pool pointer is
// stamped by a sibling init thread spawned from FUN_1800489F0's chain; on
// fast machines the foreground thread reaches FUN_1800AE410 before the
// sibling has finished, races into vt[0x1B] = FUN_18022b7b0 GetSubResource
// with this->+0xe8 still NULL, and crashes at halo1+0x22B81F.
//
// Verified live (2026-04-28) by attaching reverseme to halox + setting a sw
// breakpoint with `trace` at FUN_1800AE410 entry: pausing the foreground
// thread ~50ms gave the sibling time to stamp +0xe8 (live read confirmed
// the pointer became non-NULL during the pause), and on resume the
// FUN_18022b7b0 path completed without AV. So the fix is simply: hold
// FUN_1800AE410 at entry until +0xe8 is populated.
//
// We poll DAT_182e3c090 → +0x500 → +0xe8 in a Sleep(10ms) loop with a hard
// cap (~5s) so a structural failure doesn't deadlock the launcher. On
// timeout we log a warning and fall through to the original, which will
// reproduce the original AV — but at least we'll know.
//
// Install AFTER game_engine->initialize and BEFORE game_engine->initialize_game
// (same window as the other halo1 fixes). Idempotent across re-launches.
void halo1_install_init_render_targets_spinwait();

// Call halo1.dll's FUN_1800AE280 directly — the named-RT registration loop.
//
// FUN_1800AE280 walks PTR_DAT_181b85e60 and for each name calls
// FUN_1801F68C0(name, ...) (insert/create into the RT manager at
// DAT_181bea6b8) followed by vt[0xA8](RT, w, h, format, 0, 1, 1, 0, -2) — the
// FULL Create variant that allocates the underlying ID3D11Texture2D. At the
// end it calls InitializeRenderTargets (FUN_1800AE410) only if
// DAT_182ea2d5c != 0 (typically zero this early, so AE280 returns cleanly).
//
// Without this, our launcher's path enters initialize_game and the engine's
// own call to FUN_1800AE410 finds the named RTs unregistered (or registered
// without a backing texture) — vt[0xD0](RT, 0) returns NULL → AV at AE635.
//
// Direct call to a static function address (no hook). Must be invoked AFTER
// game_engine->initialize (which runs InitGraphics, plumbing the D3D device
// the RT Create relies on) and BEFORE initialize_game (which spawns the
// game thread that walks the RT path).
void halo1_call_register_named_render_targets();

