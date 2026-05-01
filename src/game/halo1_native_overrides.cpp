#include "halo1_native_overrides.h"

#include "game_instance_manager.h"
#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <thread>
#include <d3d11.h>
#include <dxgi.h>

#include <MinHook.h>

using namespace libmcc;

// --- halo1.dll render-config div-by-zero fix --------------------------------
//
// FUN_1801fcd40(uint width, uint height) at RVA 0x1FCD40 sets up the
// depth-mip-LBUF buffer ladder. Its body opens with:
//
//     uVar2 = max(width, height);
//     uVar2 = uVar2 / 0x1e - 1;       // <-- IDIV — faults when uVar2 < 0x1e
//     ... (bitwise OR cascade computes mip count)
//
// Our launcher path leaves the render-config global at *(0x182E3BDD8 + 0x118)
// uninitialized, so the direct caller (FUN_180453880, RVA 0x453880) reads
// width/height as 0 from offsets +0x20/+0x24 and forwards (0, 0) here. The
// real fix is to identify whatever halo3+ does that halo1 misses to populate
// that global; until that's tracked down, substituting sane non-zero
// dimensions in the hook lets the depth-mip ladder size itself successfully.
// Halo1 doesn't appear to read these dims after this call (it overrides them
// from its own framebuffer once the render thread takes over), so 1280x720 is
// safe.

static constexpr uintptr_t k_h1_div_zero_target_rva = 0x1FCD40;

using DivZeroFn = void (*)(uint32_t width, uint32_t height);
static DivZeroFn g_div_zero_orig    = nullptr;
static HMODULE   g_div_zero_module  = nullptr;

// Resolve a usable (width, height) pair when halo1 hands us zeros. Prefer the
// live swap-chain back-buffer; fall back to 1280x720 if that fails.
static void resolve_default_dims(uint32_t* out_w, uint32_t* out_h) {
	*out_w = 1280;
	*out_h = 720;

	auto* r = rasterizer();
	if (!r) return;
	auto* swap = r->get_swap_chain();
	if (!swap) return;

	DXGI_SWAP_CHAIN_DESC desc = {};
	if (SUCCEEDED(swap->GetDesc(&desc))) {
		if (desc.BufferDesc.Width  > 0) *out_w = desc.BufferDesc.Width;
		if (desc.BufferDesc.Height > 0) *out_h = desc.BufferDesc.Height;
	}
}

static void __fastcall h1_div_zero_detour(uint32_t width, uint32_t height) {
	uint32_t w = width;
	uint32_t h = height;

	// Two crash modes from the same root cause (uninitialized render-config
	// descriptor at *(DAT_182e3bdd8+0x118)+0x20/+0x24):
	//   (a) zeros — caller reads 0/0 from a missing struct
	//   (b) 0xFFFFFFFF — descriptor present but height is the "not found"
	//       sentinel; the bit-fold `((edx>>16)|edx)+1` overflows to 0 and DIVs
	// Live-snoop of MCC's running halo1 (2026-04-29) confirmed valid passes
	// look like (3840, 2160), so anything ≥ 0x1e is real. Substitute only on
	// the two known-bad sentinels.
	const bool bad_w = (w == 0) || (w == 0xFFFFFFFFu);
	const bool bad_h = (h == 0) || (h == 0xFFFFFFFFu);
	if (bad_w || bad_h) {
		uint32_t fw = 0, fh = 0;
		resolve_default_dims(&fw, &fh);
		if (fw < 0x1e) fw = 1280;
		if (fh < 0x1e) fh = 720;
		CONSOLE_LOG_WARN(
			"halo1 div-zero fix: caller passed (0x%X, 0x%X) — substituting (%u, %u)",
			width, height, fw, fh);
		if (bad_w) w = fw;
		if (bad_h) h = fh;
	}

	if (g_div_zero_orig) {
		g_div_zero_orig(w, h);
	}
}

void halo1_install_div_zero_fix() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1 div-zero fix: halo1.dll not loaded");
		return;
	}

	if (g_div_zero_module == mod) return;          // already hooked this load
	if (g_div_zero_module != nullptr) return;      // hooked a previous module — module rebase across launches not handled here

	static bool s_minhook_initialized = false;
	if (!s_minhook_initialized) {
		MH_STATUS st = MH_Initialize();
		if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			CONSOLE_LOG_ERROR("halo1 div-zero fix: MH_Initialize failed (status=%d) — game will crash", (int)st);
			return;
		}
		s_minhook_initialized = true;
	}

	void* target = reinterpret_cast<void*>(
		reinterpret_cast<uintptr_t>(mod) + k_h1_div_zero_target_rva);
	void* trampoline = nullptr;
	MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&h1_div_zero_detour), &trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo1 div-zero fix: MH_CreateHook @ %p failed (status=%d) — game will crash", target, (int)st);
		return;
	}
	g_div_zero_orig = reinterpret_cast<DivZeroFn>(trampoline);

	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo1 div-zero fix: MH_EnableHook @ %p failed (status=%d) — game will crash", target, (int)st);
		g_div_zero_orig = nullptr;
		return;
	}

	g_div_zero_module = mod;
	CONSOLE_LOG_INFO("halo1 div-zero fix installed @ %p (halo1+0x%llX)",
		target, (unsigned long long)k_h1_div_zero_target_rva);
}

// --- halo1.dll render-config field belt-and-suspenders stub ----------------
//
// FUN_1800ae410 (RVA 0xAE410), called via the per-engine init reached from
// initialize_game (FUN_18008c800), walks an 8-entry loop that reads:
//
//   (a) A render-config struct via:
//         *(longlong *)(DAT_182e3c090 + 0x118)
//           +0x10/+0x14 (low w/h) or +0x20/+0x24 (hi w/h)
//
//   (b) A named-RT lookup via:
//         FUN_1801f6fe0(DAT_181bea6b8, name)
//       where name comes from PTR_DAT_181b85e60[i*6] — a static table of
//       strings: "__FULL_8888_00__", "__FULL_8888_02__", "__HALO_RT_TEX_*"...
//
// (b) is the dominant cause of the AV at halo1+0xAE635 — the resource manager
// is empty until the engine's preload chain runs. The proper fix is to call
// PreloadCommonBegin (i_game_engine vtable slot 4) from the launcher, which
// Blam-Creation-Suite's MCC launcher does between create_game_engine and
// InitGraphics. Halo3+ stubbed slot 4 to a no-op so HaloX's launcher path
// never noticed the difference — but halo1's slot 4 still does real work.
//
// (a) is independently nice-to-have: stamping the render-config field with
// 1280x720 prevents *any* downstream null-pointer-deref on +0x10/+0x20 reads
// if some other code path zeros the field. It's purely data, not a hook.

static constexpr uintptr_t k_h1_global_struct_rva    = 0x2E3C090;
static constexpr uintptr_t k_h1_render_config_field  = 0x118;

// 256 bytes of zero — the engine reads the four (w,h) ints at +0x10..+0x27
// and we have no evidence it touches anything past +0x40, but we pad out
// generously so any other field we missed reads 0 (safe default).
struct s_h1_render_config_stub {
	uint32_t pad_00_to_0F[4];   // +0x00..+0x0F
	uint32_t w_lo;               // +0x10
	uint32_t h_lo;               // +0x14
	uint32_t pad_18_to_1F[2];    // +0x18..+0x1F
	uint32_t w_hi;               // +0x20
	uint32_t h_hi;               // +0x24
	uint8_t  pad_tail[256 - 0x28];
};
static_assert(sizeof(s_h1_render_config_stub) == 256, "stub size mismatch");
static s_h1_render_config_stub g_h1_render_config_stub = {};
static bool                    g_h1_render_config_installed = false;

void halo1_install_render_config_stub() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1 render-config stub: halo1.dll not loaded");
		return;
	}

	// Resolve the global struct's render-config-pointer field.
	uintptr_t global_struct = reinterpret_cast<uintptr_t>(mod) + k_h1_global_struct_rva;
	uintptr_t* field = reinterpret_cast<uintptr_t*>(global_struct + k_h1_render_config_field);

	// Populate the stub with sane dimensions. Prefer live swap-chain dims so
	// the rest of the engine (reading these later for camera setup, etc.)
	// gets the actual window size; fall back to 1280x720.
	uint32_t w = 1280, h = 720;
	if (auto* r = rasterizer()) {
		if (auto* sc = r->get_swap_chain()) {
			DXGI_SWAP_CHAIN_DESC desc = {};
			if (SUCCEEDED(sc->GetDesc(&desc))) {
				if (desc.BufferDesc.Width  > 0) w = desc.BufferDesc.Width;
				if (desc.BufferDesc.Height > 0) h = desc.BufferDesc.Height;
			}
		}
	}
	g_h1_render_config_stub.w_lo = w;
	g_h1_render_config_stub.h_lo = h;
	g_h1_render_config_stub.w_hi = w;
	g_h1_render_config_stub.h_hi = h;

	// Only patch if the field is currently NULL; if the engine populated it
	// (unlikely on our path, but possible after a re-launch), leave it alone.
	if (*field == 0) {
		*field = reinterpret_cast<uintptr_t>(&g_h1_render_config_stub);
		CONSOLE_LOG_INFO("halo1 render-config stub installed @ %p (halo1+0x%llX field) → struct=%p (%ux%u)",
			(void*)field, (unsigned long long)(k_h1_global_struct_rva + k_h1_render_config_field),
			(void*)&g_h1_render_config_stub, w, h);
	} else {
		CONSOLE_LOG_INFO("halo1 render-config stub: field already populated (=0x%llX), skipping",
			(unsigned long long)*field);
	}

	g_h1_render_config_installed = true;
}

// --- halo1.dll DAT_182e3bdd8 engine-context replica (snooped from MCC) -------
//
// MCC's normal launch path constructs a c_engine_context object on its heap
// via halo1!CreateGameEngine + slot 1 (initialize), then halo1's internal
// FUN_1801EEA10 stamps the pointer into halo1's DAT_182e3bdd8 global. From
// then on every halo1 subsystem reads that global to reach engine state.
//
// In halox's launch, slot 1 doesn't always populate the descriptor at +0x118
// with valid (width, height) — the field at +0x24 ends up as 0xFFFFFFFF
// (sentinel), which DIVs to 0 inside FUN_1801fcd40 (halo1+0x1FCDD9).
//
// Live-snoop of MCC's running halo1 (2026-04-29 via reverseme attach) gave us
// the exact 0x460-byte engine_ctx layout:
//   +0x000  vtable          → halo1+0x17F0C98
//   +0x008  ASCII GPU name  ("NVIDIA GeForce RTX 4090\0", 24 bytes)
//   +0x118  ptr to render-target descriptor  ← key field — valid w/h here
//   +0x138/+0x140/+0x148  3× pooled subsystem objects (vtable=halo1+0x17F9D10)
//   +0x178  duplicate of +0x118
//   +0x224/+0x228  resolution mirror (u32 width, u32 height)
//   +0x340  UTF-16 GPU name
// Other fields are inert data (refresh, gamma, etc.) or richer object graphs
// at +0x260/+0x270/+0x330/+0x338 which we leave NULL until something faults.
//
// The descriptor (~256 bytes) needs only:
//   +0x10/+0x14  display mode w/h
//   +0x20/+0x24  the w/h that FUN_1801fcd40 reads
//   +0x4C        ASCII GPU name copy
// Everything else can stay zero — the snoop showed the rest is mostly internal
// halo1 state pointers populated lazily.
//
// Defensive policy: only stamp DAT_182e3bdd8 if it's currently NULL, OR if
// the existing descriptor's +0x24 reads as the 0xFFFFFFFF sentinel. We never
// overwrite a fully-populated engine_ctx that slot 1 successfully built.

static constexpr uintptr_t k_h1_engine_ctx_global_rva = 0x2E3BDD8;
// Sibling engine-state global. FUN_1800AE280 reads (DAT_182e3c090+0x118) as
// a descriptor pointer, identical layout to (DAT_182e3bdd8+0x118). It's also
// the global the spinwait reaches +0x500 through. Stamping it with the same
// engine_ctx blob makes both reads return our valid descriptor.
static constexpr uintptr_t k_h1_engine_state_global_rva = 0x2E3C090;
static constexpr uintptr_t k_h1_engine_ctx_vtable_rva = 0x17F0C98;
static constexpr uintptr_t k_h1_pooled_obj_vtable_rva = 0x17F9D10;
// CreateGameEngine allocates 0x460 but the real class is larger — live snoop
// of MCC (2026-04-29) showed populated heap pointers at +0x4D8 and +0x4E8,
// and FUN_1801EEA10 crashed on us at +0x19F because it reads this->[0x4D8]
// and our 0x460-sized stub leaked into adjacent halox BSS (non-zero garbage
// failed the `test rcx,rcx; jz` skip). Oversize to 0x1000 with zero-pad so
// any "load past 0x460" reads zero and the implicit-null guards short-circuit.
static constexpr size_t    k_h1_engine_ctx_size       = 0x1000;
static constexpr size_t    k_h1_descriptor_size       = 0x400;
static constexpr size_t    k_h1_pooled_obj_size       = 0x200;

struct alignas(32) s_h1_engine_ctx_blob {
	uint8_t bytes[k_h1_engine_ctx_size];
};
struct alignas(16) s_h1_descriptor_blob {
	uint8_t bytes[k_h1_descriptor_size];
};
struct alignas(16) s_h1_pooled_obj_blob {
	uint8_t bytes[k_h1_pooled_obj_size];
};

static s_h1_engine_ctx_blob g_h1_engine_ctx_storage   = {};
static s_h1_descriptor_blob g_h1_descriptor_storage   = {};
static s_h1_pooled_obj_blob g_h1_pooled_obj_storage[3] = {};
static bool                 g_h1_engine_ctx_installed = false;

void halo1_install_engine_ctx_stub() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;
	if (g_h1_engine_ctx_installed) return;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1 engine_ctx stub: halo1.dll not loaded");
		return;
	}
	uintptr_t base = reinterpret_cast<uintptr_t>(mod);

	// Check current state of DAT_182e3bdd8.
	auto** global_slot = reinterpret_cast<uint8_t**>(base + k_h1_engine_ctx_global_rva);
	uint8_t* existing  = *global_slot;
	bool needs_stamp = (existing == nullptr);
	if (!needs_stamp) {
		// Slot 1 ran. Check whether its descriptor at +0x118 has the bad
		// sentinel — if so, our stamp still wins; otherwise leave it alone.
		auto** desc_ptr = reinterpret_cast<uint8_t**>(existing + 0x118);
		uint8_t* desc   = *desc_ptr;
		if (desc) {
			uint32_t h = *reinterpret_cast<uint32_t*>(desc + 0x24);
			if (h == 0xFFFFFFFFu || h == 0) {
				CONSOLE_LOG_WARN("halo1 engine_ctx stub: existing descriptor has bad height=0x%X — overriding", h);
				needs_stamp = true;
			}
		} else {
			CONSOLE_LOG_WARN("halo1 engine_ctx stub: existing engine_ctx has NULL descriptor — overriding");
			needs_stamp = true;
		}
	}
	if (!needs_stamp) {
		CONSOLE_LOG_INFO("halo1 engine_ctx stub: DAT_182e3bdd8 already populated with valid descriptor — skipping");
		g_h1_engine_ctx_installed = true;
		return;
	}

	// Resolve real screen dims from the swapchain (or fall back).
	uint32_t w = 1280, h = 720;
	resolve_default_dims(&w, &h);
	if (w < 0x1e) w = 1280;
	if (h < 0x1e) h = 720;

	// Build the descriptor (~256 bytes). Mirror MCC's snoop layout for the
	// fields that matter; leave the rest zero.
	uint8_t* desc = g_h1_descriptor_storage.bytes;
	memset(desc, 0, k_h1_descriptor_size);
	*reinterpret_cast<uint32_t*>(desc + 0x10) = w;
	*reinterpret_cast<uint32_t*>(desc + 0x14) = h;
	*reinterpret_cast<uint32_t*>(desc + 0x18) = 0x20;       // BPP/format hint
	*reinterpret_cast<float*>   (desc + 0x1C) = 1.0f;
	*reinterpret_cast<uint32_t*>(desc + 0x20) = w;          // ← read by FUN_1801fcd40
	*reinterpret_cast<uint32_t*>(desc + 0x24) = h;          // ← read by FUN_1801fcd40
	memcpy(desc + 0x4C, "halox-stub-display\0", 19);

	// Build the 3 pooled subsystem objects (+0x138/+0x140/+0x148 targets).
	// Live snoop showed them all as: vtable @ halo1+0x17F9D10, u32 2 at +0x18,
	// rest zero. Stamp the same.
	for (int i = 0; i < 3; i++) {
		uint8_t* obj = g_h1_pooled_obj_storage[i].bytes;
		memset(obj, 0, k_h1_pooled_obj_size);
		*reinterpret_cast<void**>   (obj + 0x00) = reinterpret_cast<void*>(base + k_h1_pooled_obj_vtable_rva);
		*reinterpret_cast<uint32_t*>(obj + 0x18) = 2;
	}

	// Build the engine_ctx itself.
	uint8_t* ctx = g_h1_engine_ctx_storage.bytes;
	memset(ctx, 0, k_h1_engine_ctx_size);
	*reinterpret_cast<void**>(ctx + 0x000) = reinterpret_cast<void*>(base + k_h1_engine_ctx_vtable_rva);
	memcpy(ctx + 0x008, "halox-stub-display\0", 19);

	*reinterpret_cast<void**>(ctx + 0x118) = desc;          // descriptor ptr (primary)
	*reinterpret_cast<void**>(ctx + 0x178) = desc;          // descriptor ptr (mirror)
	*reinterpret_cast<void**>(ctx + 0x138) = g_h1_pooled_obj_storage[0].bytes;
	*reinterpret_cast<void**>(ctx + 0x140) = g_h1_pooled_obj_storage[1].bytes;
	*reinterpret_cast<void**>(ctx + 0x148) = g_h1_pooled_obj_storage[2].bytes;
	*reinterpret_cast<uint32_t*>(ctx + 0x224) = w;          // resolution mirror
	*reinterpret_cast<uint32_t*>(ctx + 0x228) = h;
	*reinterpret_cast<float*>   (ctx + 0x1A0) = 0.95f;      // gamma/sens-ish floats from snoop
	*reinterpret_cast<float*>   (ctx + 0x1A8) = 1.0f;

	*global_slot = ctx;

	// Also stamp the sibling DAT_182e3c090 if it's NULL — FUN_1800AE280
	// reads (DAT_182e3c090+0x118) for the same descriptor; without this it
	// AVs at halo1+0xAE31D on the first iteration. Other halo1 paths use
	// (DAT_182e3c090+0x500) which our zero-padded ctx returns as NULL —
	// those callers either NULL-check or get caught by the existing
	// spinwait/vtable-swap belt-and-suspenders.
	auto** state_slot = reinterpret_cast<uint8_t**>(base + k_h1_engine_state_global_rva);
	if (*state_slot == nullptr) {
		*state_slot = ctx;
		CONSOLE_LOG_INFO("halo1 engine_state stub installed @ DAT_182e3c090=%p (sharing ctx=%p)",
			(void*)state_slot, (void*)ctx);
	}

	g_h1_engine_ctx_installed = true;

	CONSOLE_LOG_INFO("halo1 engine_ctx stub installed @ DAT_182e3bdd8=%p ctx=%p desc=%p (%ux%u)",
		(void*)global_slot, (void*)ctx, (void*)desc, w, h);
}

// --- halo1.dll PreloadCommonBegin / PreloadLevelBegin direct calls ----------
//
// These are the missing init steps in HaloX's launcher. Blam-Creation-Suite's
// MCC launcher (Framework/GameFramework/MCC/game_launcher.cpp,
// c_game_launcher::launch_mcc_game) invokes them between create_game_engine
// and InitGraphics:
//
//     game_engine->PreloadCommonBegin(c_render::s_device);
//     game_engine->PreloadLevelBegin(game_options->map_id);
//     game_engine->InitGraphics(device, ctx, swap, swap);
//     game_engine->PlayGame(host, options);
//
// On the IGameEngine V2 vtable layout (build >= 1367), slots 4 / 5 are
// PreloadCommonBegin / PreloadLevelBegin. The libmcc::i_game_engine binding
// declares the same vtable shape but names the two slots `_` and `__` because
// halo3+ stubbed them to no-ops. halo1 still does the real preload work in
// those slots — and it's substantial:
//   slot 4 (FUN_180095700): allocates DAT_182e3d2f8 (preload state struct),
//                          reads game.cfg [Preload.usePaks/usePreloadUui],
//                          spins up the UUI Saber Preload thread.
//   slot 5 (FUN_180095910): per-map preload setup, sets the
//                          DAT_182e3d2f8 + 0xcd01a8 "level-preloaded" flag
//                          that PlayGame's first if-block reads.
//
// Without slot 4 having run, PlayGame's leading branch is skipped and a chain
// of subsequent init code paths leave the resource manager (DAT_181bea6b8)
// empty — which is what makes FUN_1801f6fe0 return NULL inside FUN_1800ae410
// and produce the AV at halo1+0xAE635.
//
// We invoke them via a manual vtable lookup (no MinHook involvement). Pure
// virtual call.

void halo1_call_preload_common_begin(void* engine, void* device) {
	if (engine == nullptr) return;
	auto vtable = *reinterpret_cast<void***>(engine);
	if (vtable == nullptr) return;
	using Slot4Fn = void(__fastcall*)(void* /*this*/, void* /*device*/);
	auto fn = reinterpret_cast<Slot4Fn>(vtable[4]);
	CONSOLE_LOG_INFO("halo1_call_preload_common_begin: this=%p vt=%p slot4=%p device=%p",
		engine, (void*)vtable, (void*)fn, device);
	fn(engine, device);
	CONSOLE_LOG_INFO("halo1_call_preload_common_begin: returned");
}

void halo1_call_preload_level_begin(void* engine, int map_index) {
	if (engine == nullptr) return;
	auto vtable = *reinterpret_cast<void***>(engine);
	if (vtable == nullptr) return;
	// halo1's slot 5 takes (this, int map_index). Negative map_index is a
	// safe no-op path inside the function (the body is gated on
	// `-1 < param_2 && DAT_182e3d2f8 != 0`).
	using Slot5Fn = void(__fastcall*)(void* /*this*/, int /*map_index*/);
	auto fn = reinterpret_cast<Slot5Fn>(vtable[5]);
	CONSOLE_LOG_INFO("halo1_call_preload_level_begin: this=%p vt=%p slot5=%p map_index=%d",
		engine, (void*)vtable, (void*)fn, map_index);
	fn(engine, map_index);
	CONSOLE_LOG_INFO("halo1_call_preload_level_begin: returned");
}

// halo1.dll's game thread halts at halo1+0xAD7D58 (0xBEEF0117) when
// FUN_180AC49B8 evaluates `(DAT_182E3B9A0 != 0 && *DAT_182D91330 == 0)` true.
//
// 2026-04-30 watchpoint snoop of MCC's running halo 1 (PID 697044) showed
// that DAT_182D91330, DAT_182BAA300, and DAT_182B259C0 stay NULL across an
// entire halo 1 launch in MCC — only DAT_182E3B9A0 is written (by
// initialize_game). MCC simply never reaches FUN_180AC49B8's halt path,
// because its engine-driver code path (MCC+0x1F05F2 → halo1!initialize_game,
// then a sequence of MCC functions that drive halo 1 directly) skips the
// FUN_180AC2528 / FUN_180AAE97C / SYS::haloInit chain entirely.
//
// Halox previously tried to call FUN_180AC2528/FUN_18008C800/FUN_180AAE97C
// to allocate the engine state — that triggered downstream AVs at
// halo1+0xBF06E7 (FUN_180BF06C0 reading DAT_182BAA300+0x324B0) and others,
// because those functions' downstream consumers want state that other
// subsystems should set up. Halox doesn't run those subsystems — and
// neither does MCC.
//
// Final approach: NOP the BEEF0117 halt-call so the engine thread can
// continue past it (matching MCC's effective behavior — MCC never runs
// the halt-call site at all), and stop trying to fake the SYS::haloInit
// state. Whatever the engine thread really needs comes from MCC's
// post-initialize_game driver loop, which we still need to discover.

static constexpr uintptr_t k_h1_halt_call_rva       = 0xAC4A83;
static constexpr size_t    k_h1_halt_call_size      = 5;
static constexpr uintptr_t k_h1_launch_state_rva    = 0x2EA32F0;

// FUN_1803bbe90 (the engine pump's per-tick body) opens with:
//     test byte ptr [DAT_182e3c3e0], 4   ; F6 05 ?? ?? ?? ?? 04
//     jne  <crash branch>                 ; 0F 85 ?? ?? ?? ??
// The crash branch ends up calling FUN_180BF06C0 which AVs reading
// DAT_182BAA300+0x324B0 (DAT_182BAA300 is NULL in halox's launcher path).
// Live snoop of MCC's running halo 1 (PID 697044, 2026-04-30) showed
// DAT_182E3C3E0 = 0 throughout — MCC always takes the safe branch.
// Halox somehow has bit-2 set, sending the engine into the crash branch.
//
// Cheapest deterministic fix: patch the imm8 at halo1+0x3BBE9C from 0x04
// to 0x00. With `test r/m8, 0` ZF is always set, so the following JNE
// is never taken — engine always runs the MCC-equivalent path.
static constexpr uintptr_t k_h1_pump_test_imm_rva = 0x3BBE9C;

void halo1_call_engine_subsystem_init() {
	static bool s_called = false;
	if (s_called) return;

	HMODULE h = GetModuleHandleW(L"halo1.dll");
	if (h == nullptr) {
		CONSOLE_LOG_WARN("halo1_call_engine_subsystem_init: halo1.dll not loaded — skipping");
		return;
	}
	auto* h_base = reinterpret_cast<uint8_t*>(h);

	auto* halt_call = h_base + k_h1_halt_call_rva;
	if (halt_call[0] == 0xE8) {
		DWORD old_prot = 0;
		if (VirtualProtect(halt_call, k_h1_halt_call_size, PAGE_EXECUTE_READWRITE, &old_prot)) {
			for (size_t i = 0; i < k_h1_halt_call_size; ++i) halt_call[i] = 0x90;
			DWORD restore_prot = 0;
			VirtualProtect(halt_call, k_h1_halt_call_size, old_prot, &restore_prot);
			FlushInstructionCache(GetCurrentProcess(), halt_call, k_h1_halt_call_size);
			CONSOLE_LOG_INFO("halo1_call_engine_subsystem_init: NOP'd BEEF0117 halt call @ halo1+0x%llX",
				(unsigned long long)k_h1_halt_call_rva);
		}
	} else if (halt_call[0] == 0x90) {
		CONSOLE_LOG_INFO("halo1_call_engine_subsystem_init: halt call already NOP'd");
	} else {
		CONSOLE_LOG_WARN("halo1_call_engine_subsystem_init: unexpected byte 0x%02X at halo1+0x%llX",
			halt_call[0], (unsigned long long)k_h1_halt_call_rva);
	}

	// Force the external-launch state machine to "complete" (state 0xC).
	// FUN_180B27184 is a state machine keyed on (int)DAT_182EA32F0. State 1
	// calls FUN_180B26820 → FUN_180BF06C0 which AVs reading uninitialized
	// DAT_182BAA300+0x324B0 in halox's launcher path. State 0xC ("done")
	// makes FUN_180B27184 a no-op — its body's if/else-if chain bypasses
	// every state action when state is 0xC, so the crash chain never runs.
	// MCC has DAT_182EA32F0 = 0 in normal halo 1 because it never reaches
	// FUN_180B27184 at all (its callers' gate `*DAT_182D91330 == 0` AV's
	// in MCC because MCC has DAT_182D91330 = NULL, but execution never
	// reaches that gate either). Halox does reach the gate with a non-NULL
	// DAT_182D91330, so the safest patch is to force the state machine
	// itself to be a no-op.
	auto* state_slot = reinterpret_cast<int*>(h_base + k_h1_launch_state_rva);
	{
		DWORD old_prot = 0;
		if (VirtualProtect(state_slot, sizeof(int), PAGE_READWRITE, &old_prot)) {
			int prev = *state_slot;
			*state_slot = 0xC;
			DWORD restore_prot = 0;
			VirtualProtect(state_slot, sizeof(int), old_prot, &restore_prot);
			CONSOLE_LOG_INFO("halo1_call_engine_subsystem_init: stamped DAT_182EA32F0=0xC (was %d) — launch state machine forced complete",
				prev);
		}
	}

	// Patch the FUN_1803bbe90 pump-test imm8 from 4 to 0 so the engine
	// always takes the MCC-equivalent safe branch and never enters the
	// FUN_180BF06C0 / FUN_180AC2528 crash chain.
	auto* pump_imm = h_base + k_h1_pump_test_imm_rva;
	if (pump_imm[0] == 0x04) {
		DWORD old_prot = 0;
		if (VirtualProtect(pump_imm, 1, PAGE_EXECUTE_READWRITE, &old_prot)) {
			pump_imm[0] = 0x00;
			DWORD restore_prot = 0;
			VirtualProtect(pump_imm, 1, old_prot, &restore_prot);
			FlushInstructionCache(GetCurrentProcess(), pump_imm, 1);
			CONSOLE_LOG_INFO("halo1_call_engine_subsystem_init: patched FUN_1803bbe90 test-imm to 0 @ halo1+0x%llX",
				(unsigned long long)k_h1_pump_test_imm_rva);
		}
	} else if (pump_imm[0] == 0x00) {
		CONSOLE_LOG_INFO("halo1_call_engine_subsystem_init: pump test-imm already patched");
	} else {
		CONSOLE_LOG_WARN("halo1_call_engine_subsystem_init: unexpected byte 0x%02X at halo1+0x%llX (want 0x04)",
			pump_imm[0], (unsigned long long)k_h1_pump_test_imm_rva);
	}

	s_called = true;
}

// --- Heap-only vtable swap on RT instances -----------------------------------
//
// See halo1_native_overrides.h for the full rationale. Short version:
// FUN_1800AE410 dereferences a NULL subresource pointer at AE635 in our
// launcher path. We override `vt[0x1A]` (GetSubResource) and `vt[0x1B]`
// (GetMipSubResource) on each RT instance by rewriting its vtable pointer to
// a writable copy whose two affected slots return a stub object with no-op
// AddRef/Release.

static constexpr uintptr_t k_h1_rt_manager_global_rva = 0x1BEA6B8;
static constexpr uintptr_t k_h1_rt_manager_cs_rva     = 0x1BAA028;

// Stub vtable for the subresource we hand back from the overridden slots.
// We don't know every slot the engine might call on the result, so we fill
// generously with a no-op-returning-zero default.
static constexpr int k_stub_vtable_slots = 64;
static void* g_stub_sub_vtable[k_stub_vtable_slots] = {};

struct s_stub_subresource {
	void**   vtable;
	uint32_t refcount;
	uint32_t pad1;
	uint8_t  pad[256 - 16];
};
static_assert(sizeof(s_stub_subresource) == 256, "stub size mismatch");
static s_stub_subresource g_stub_subresource = {};

// Default no-op vtable handlers. x64 MSVC calling convention puts `this` in
// RCX, args in RDX, R8, R9 — so a plain function with `void*` first arg gets
// the engine's `this` pointer correctly.
static void     stub_dtor(void*)                      {}
static uint32_t stub_addref(void*)                    { return 1; }
static uint32_t stub_release(void*)                   { return 1; }
static void*    stub_return_null(void*, ...)          { return nullptr; }
static int64_t  stub_return_zero_i(void*, ...)        { return 0; }

// Per-original-vtable cache: we need to keep a writable copy per unique
// halo1 RT vtable seen, with slots 0x1A and 0x1B overridden. Multiple RT
// instances can share the same original vtable; one copy serves all.
struct s_swapped_vt_entry {
	void*  original;
	void** copy;
};
static constexpr int k_max_unique_vts = 8;
static s_swapped_vt_entry g_swapped_vts[k_max_unique_vts] = {};
static int                g_swapped_vt_count = 0;
static SRWLOCK            g_swapped_vt_lock = SRWLOCK_INIT;

// Real d3d11 resource we hand back from the overridden slots. Live RE in MCC
// (halo1 running) showed the value stored in halo1's RT subresource tables is
// a pointer to a real d3d11 COM object — its first 8 bytes are a vtable in
// d3d11.dll (verified via *(*(RT+0xE8)) → 0x000001FFBD1930F0 → first qword
// 0x00007FFC2E3107F8, in d3d11.dll's range). Our previous stub returned a
// fake C++ object with no d3d11 vtable; that leaked into d3d11's command
// processor and AV'd at d3d11+0x10074D. Returning a real ID3D11Texture2D
// (which IS-A ID3D11Resource) keeps d3d11's hazard checks walking valid
// memory.
static ID3D11Texture2D*          g_halo1_stub_texture = nullptr;
static ID3D11ShaderResourceView* g_halo1_stub_srv     = nullptr;
static ID3D11RenderTargetView*   g_halo1_stub_rtv     = nullptr;

void halo1_init_stub_d3d11_resource(ID3D11Device* device) {
	if (g_halo1_stub_texture) return; // idempotent
	if (!device) return;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width  = 4;
	desc.Height = 4;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage  = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

	HRESULT hr = device->CreateTexture2D(&desc, nullptr, &g_halo1_stub_texture);
	if (FAILED(hr) || !g_halo1_stub_texture) {
		CONSOLE_LOG_WARN("halo1 stub d3d11 resource: CreateTexture2D failed hr=0x%08lX", hr);
		return;
	}
	hr = device->CreateShaderResourceView(g_halo1_stub_texture, nullptr, &g_halo1_stub_srv);
	if (FAILED(hr)) {
		CONSOLE_LOG_WARN("halo1 stub d3d11 resource: CreateShaderResourceView failed hr=0x%08lX", hr);
	}
	hr = device->CreateRenderTargetView(g_halo1_stub_texture, nullptr, &g_halo1_stub_rtv);
	if (FAILED(hr)) {
		CONSOLE_LOG_WARN("halo1 stub d3d11 resource: CreateRenderTargetView failed hr=0x%08lX", hr);
	}
	CONSOLE_LOG_INFO("halo1 stub d3d11 resource: tex=%p srv=%p rtv=%p",
		(void*)g_halo1_stub_texture, (void*)g_halo1_stub_srv, (void*)g_halo1_stub_rtv);
}

// RT-class slot overrides. Engine calls `(this->vt[0xD0/8])(this, idx)` for
// the GetSubResource accessor and `(this->vt[0xD8/8])(this, face, mip, ?)`
// for GetMipSubResource. The two slots feed DIFFERENT d3d11 paths:
//
//   slot 0x1A (vt[0xD0], GetSubResource): result fed to PSSetShaderResources
//     and hazard tracking — needs ID3D11ShaderResourceView*.
//
//   slot 0x1B (vt[0xD8], GetMipSubResource): result placed into a 4-element
//     RT array which FUN_180205E40 hands to a wrapper around OMSetRenderTargets
//     (`vt[0x108] on param_1[0x19c]`, called as `(num, RT_array, depth)`).
//     d3d11's OMSetRenderTargets expects ID3D11RenderTargetView*.
//
// Strategy: chain to original ONLY when the RT looks initialized (its +0xE8
// d3d11 resource pointer is non-null). Broken RTs (the ones that AV'd in the
// original FUN_1800AE410 path) have +0xE8 = NULL and get our 4x4 stub. Init'd
// RTs return their real ID3D11SRV/RTV so world rendering goes to actual
// textures instead of our 4x4.
//
// The original vtable back-pointer is stashed at copy[k_orig_vt_backptr_slot]
// during get_or_create_vt_copy — no per-call lookup needed.
//
// The chained call is wrapped in:
//   1) c_exception_log_suppressor — silences the vectored first-chance
//      exception handler so its dbghelp walk doesn't corrupt under concurrent
//      AV firings from many game threads.
//   2) __try / __except (EXCEPTION_EXECUTE_HANDLER) — locally catches AVs
//      from chain calls into partially-init halo1 internals so we can fall
//      back to the 4x4 stub.
// MSVC forbids mixing C++ object unwinding with SEH in one function, so
// the SEH-wrapping helpers are written without C++ destructors and the
// suppressor RAII lives in a parent C++ function.
static constexpr int k_orig_vt_backptr_slot = 255;

// Gate for the chain-to-original. The AV in halox was at FUN_18022b7b0+0x6F:
//
//   mov rax, [rax+0xE8]            ; lVar1 = *(ctx+0xE8)
//   ...
//   movsx r9d, byte [r11+0x1A]     ; face_count = (signed char) *(this+0x1A)
//   ...
//   mov rax, [rax + rcx*8]         ; ← AV here
//
// The final index uses face_count as a signed char. On a partially-init RT
// where +0x1A is uninit (often 0xFF), face_count sign-extends to -1, the
// computed index is hugely negative, and the read AVs. MCC live capture
// (2026-04-30) showed face_count is 1 (2D RT) or 6 (cubemap) on valid RTs.
//
// Safe-call predicate:
//   (a) +0x1A must be a sane positive face count (1 or 6).
//   (b) The d3d11 resource array pointer at +0xE8 (face=0) or +0xF8 (face!=0)
//       must be non-null. In MCC, RT_manager+0x1D0 bit 15 is clear, so
//       FUN_1800ad5f0 returns `this` itself — the deref is on `this` directly.
//       In halox we make the same assumption (defensive — if the manager
//       bits are different, we miss valid RTs but don't break broken ones).
static inline bool rt_safe_to_chain_subres(void* self) {
	if (!self) return false;
	auto p = reinterpret_cast<uintptr_t>(self);
	int8_t face_count = *reinterpret_cast<int8_t*>(p + 0x1A);
	if (face_count != 1 && face_count != 6) return false;
	void* arr = *reinterpret_cast<void**>(p + 0xE8);
	return arr != nullptr;
}
static inline bool rt_safe_to_chain_mip(void* self, int face) {
	if (!self) return false;
	auto p = reinterpret_cast<uintptr_t>(self);
	int8_t face_count = *reinterpret_cast<int8_t*>(p + 0x1A);
	if (face_count != 1 && face_count != 6) return false;
	uintptr_t off = (face == 0) ? 0xE8 : 0xF8;
	void* arr = *reinterpret_cast<void**>(p + off);
	return arr != nullptr;
}

static inline void** orig_vt_from_self(void* self) {
	void** my_vt = *reinterpret_cast<void***>(self);
	return reinterpret_cast<void**>(my_vt[k_orig_vt_backptr_slot]);
}

// SEH-wrapping helpers — no C++ objects with destructors allowed in these.
static void* try_call_orig_subresource(void* fn, void* self, int idx) {
	using Fn = void* (__fastcall*)(void*, int);
	void* result = nullptr;
	__try {
		result = reinterpret_cast<Fn>(fn)(self, idx);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		result = nullptr;
	}
	return result;
}
// FUN_18022b7b0 (halo1's RT get_mip_subresource) is a 5-arg function: the 5th
// arg is a signed int the engine writes at [rsp+0x20] before the call (stored
// at [rsp+0x28] from the callee's view). Live RE confirmed it as a sign-ext
// byte from the caller's `[r15+0x43]`. The function uses arg5 as part of an
// index calculation: ecx = arg5 * face_count + R9 + R8, then loads from
// `[table + rcx*8]`. Forwarding through a 4-arg typedef leaves [rsp+0x28] as
// stack garbage, the index computes to a bogus value, and the original
// returns a pointer into halo1's internal heap-globals region instead of a
// real subresource — the engine then does `vt[AddRef]` on that and AVs at
// halo1+0x2D26540 (.data BSS, NX violation).
static void* try_call_orig_mip_subresource(void* fn, void* self, int face, int mip, int samples, int arg5) {
	using Fn = void* (__fastcall*)(void*, int, int, int, int);
	void* result = nullptr;
	__try {
		result = reinterpret_cast<Fn>(fn)(self, face, mip, samples, arg5);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		result = nullptr;
	}
	return result;
}

// Engine's per-store AddRef at halo1+0xAE635 is NO LONGER NOP'd, so each
// stored result gets a +1 refcount automatically. We just return — caller
// AddRefs us, and the destructor's Release per stored entry then balances.
static void* stub_rt_get_subresource(void* self, int idx) {
	if (rt_safe_to_chain_subres(self)) {
		void** orig = orig_vt_from_self(self);
		if (orig && orig[0x1A]) {
			c_exception_log_suppressor suppress;
			void* result = try_call_orig_subresource(orig[0x1A], self, idx);
			if (result) return result;
		}
	}
	if (g_halo1_stub_srv)     return g_halo1_stub_srv;
	if (g_halo1_stub_texture) return g_halo1_stub_texture;
	return &g_stub_subresource;
}
static void* stub_rt_get_mip_subresource(void* self, int face, int mip, int samples, int arg5) {
	if (rt_safe_to_chain_mip(self, face)) {
		void** orig = orig_vt_from_self(self);
		if (orig && orig[0x1B]) {
			c_exception_log_suppressor suppress;
			void* result = try_call_orig_mip_subresource(orig[0x1B], self, face, mip, samples, arg5);
			if (result) return result;
		}
	}
	if (g_halo1_stub_rtv)     return g_halo1_stub_rtv;
	if (g_halo1_stub_texture) return g_halo1_stub_texture;
	return &g_stub_subresource;
}

static void initialize_stub_subresource_once() {
	static bool s_initialized = false;
	if (s_initialized) return;
	for (int i = 0; i < k_stub_vtable_slots; i++) {
		g_stub_sub_vtable[i] = reinterpret_cast<void*>(&stub_return_zero_i);
	}
	g_stub_sub_vtable[0] = reinterpret_cast<void*>(&stub_dtor);
	g_stub_sub_vtable[1] = reinterpret_cast<void*>(&stub_addref);
	g_stub_sub_vtable[2] = reinterpret_cast<void*>(&stub_release);
	g_stub_subresource.vtable   = g_stub_sub_vtable;
	g_stub_subresource.refcount = 1;
	s_initialized = true;
}

// Look up (or allocate) a writable copy of the given original vtable, with
// slots 0x1A / 0x1B overridden.
static void** get_or_create_vt_copy(void* original_vt) {
	AcquireSRWLockExclusive(&g_swapped_vt_lock);

	for (int i = 0; i < g_swapped_vt_count; i++) {
		if (g_swapped_vts[i].original == original_vt) {
			void** ret = g_swapped_vts[i].copy;
			ReleaseSRWLockExclusive(&g_swapped_vt_lock);
			return ret;
		}
	}

	if (g_swapped_vt_count >= k_max_unique_vts) {
		CONSOLE_LOG_WARN("halo1 RT vtable swap: exceeded %d unique vtables", k_max_unique_vts);
		ReleaseSRWLockExclusive(&g_swapped_vt_lock);
		return nullptr;
	}

	// Vtables can be large. 256 slots = 2KB is generous for any RT class.
	constexpr size_t copy_bytes = 256 * sizeof(void*);
	void** copy = static_cast<void**>(VirtualAlloc(
		nullptr, copy_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!copy) {
		ReleaseSRWLockExclusive(&g_swapped_vt_lock);
		return nullptr;
	}

	memcpy(copy, original_vt, copy_bytes);
	copy[0x1A] = reinterpret_cast<void*>(&stub_rt_get_subresource);    // offset 0xD0
	copy[0x1B] = reinterpret_cast<void*>(&stub_rt_get_mip_subresource); // offset 0xD8
	copy[k_orig_vt_backptr_slot] = original_vt; // back-pointer for chain-to-original

	g_swapped_vts[g_swapped_vt_count++] = { original_vt, copy };
	int idx = g_swapped_vt_count - 1;
	ReleaseSRWLockExclusive(&g_swapped_vt_lock);

	CONSOLE_LOG_INFO("halo1 RT vtable swap: cached new vtable copy (#%d) original=%p copy=%p",
		idx, original_vt, (void*)copy);
	return copy;
}

// Returns true if `vt` is one of our known writable copies (i.e., already
// swapped — skip).
static bool is_already_swapped(void* vt) {
	AcquireSRWLockShared(&g_swapped_vt_lock);
	bool found = false;
	for (int i = 0; i < g_swapped_vt_count; i++) {
		if (g_swapped_vts[i].copy == vt) { found = true; break; }
	}
	ReleaseSRWLockShared(&g_swapped_vt_lock);
	return found;
}

int halo1_swap_rt_vtables_once() {
	initialize_stub_subresource_once();

	auto im = game_instance_manager();
	if (!im) return 0;
	if (im->get_game() != _module_halo1) return 0;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) return 0;

	uintptr_t mod_base = reinterpret_cast<uintptr_t>(mod);
	void* manager = *reinterpret_cast<void**>(mod_base + k_h1_rt_manager_global_rva);
	if (!manager) return -1; // not allocated yet

	LPCRITICAL_SECTION cs = reinterpret_cast<LPCRITICAL_SECTION>(mod_base + k_h1_rt_manager_cs_rva);
	EnterCriticalSection(cs);

	void** array = *reinterpret_cast<void***>((uintptr_t)manager + 0x1B8);
	int    count = *reinterpret_cast<int*>((uintptr_t)manager + 0x1C0);

	int swapped = 0;
	if (array && count > 0 && count < 4096) { // sanity bound
		for (int i = 0; i < count; i++) {
			void* entry = array[i];
			if (!entry) continue;
			void* current_vt = *reinterpret_cast<void**>(entry);
			if (!current_vt) continue;
			if (is_already_swapped(current_vt)) continue;
			void** copy = get_or_create_vt_copy(current_vt);
			if (!copy) continue;
			*reinterpret_cast<void***>(entry) = copy;
			swapped++;
		}
	}

	LeaveCriticalSection(cs);

	if (swapped > 0) {
		CONSOLE_LOG_INFO("halo1 RT vtable swap: swapped %d new entries (total table count=%d)",
			swapped, count);
	}
	return swapped;
}

void halo1_start_rt_vtable_swap_poller() {
	static bool s_started = false;
	if (s_started) return;
	s_started = true;

	std::thread([](){
		// Poll for up to 5 seconds total at 25ms cadence (200 iterations).
		// Each iteration is an idempotent walk that only swaps NEW entries.
		// We don't stop early — entries can appear over time as the engine
		// adds them in stages (preload, level setup, etc.).
		for (int i = 0; i < 200; i++) {
			halo1_swap_rt_vtables_once();
			Sleep(25);
		}
		CONSOLE_LOG_INFO("halo1 RT vtable swap: poller exiting after 5s");
	}).detach();
}

// --- halo1.dll runtime byte-patch (RT subresource AddRef NOP) ----------------

static constexpr uintptr_t k_h1_rt_addref_patch_rva = 0xAE635;
// Bytes to replace: `48 8b 10 ff 52 08` (mov rdx,[rax]; call [rdx+8])
static constexpr size_t    k_h1_rt_addref_patch_len = 6;

void halo1_install_rt_subresource_addref_nop() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1 RT addref NOP: halo1.dll not loaded");
		return;
	}

	auto* patch_addr = reinterpret_cast<uint8_t*>(
		reinterpret_cast<uintptr_t>(mod) + k_h1_rt_addref_patch_rva);

	// Idempotent: if the bytes are already NOPs, skip.
	bool already_patched = true;
	for (size_t i = 0; i < k_h1_rt_addref_patch_len; i++) {
		if (patch_addr[i] != 0x90) { already_patched = false; break; }
	}
	if (already_patched) {
		CONSOLE_LOG_INFO("halo1 RT addref NOP: already applied @ %p", patch_addr);
		return;
	}

	// Verify the original bytes match what we expect, otherwise abort —
	// this guards against a halo1.dll update that shifted the offset.
	static const uint8_t expected[k_h1_rt_addref_patch_len] = {
		0x48, 0x8b, 0x10, 0xff, 0x52, 0x08
	};
	for (size_t i = 0; i < k_h1_rt_addref_patch_len; i++) {
		if (patch_addr[i] != expected[i]) {
			CONSOLE_LOG_ERROR(
				"halo1 RT addref NOP: byte mismatch @ %p offset %zu — got 0x%02X, expected 0x%02X. "
				"halo1.dll layout may have shifted; aborting patch.",
				patch_addr, i, patch_addr[i], expected[i]);
			return;
		}
	}

	DWORD old_protect = 0;
	if (!VirtualProtect(patch_addr, k_h1_rt_addref_patch_len, PAGE_EXECUTE_READWRITE, &old_protect)) {
		CONSOLE_LOG_ERROR("halo1 RT addref NOP: VirtualProtect(RWX) failed: %lu", GetLastError());
		return;
	}

	for (size_t i = 0; i < k_h1_rt_addref_patch_len; i++) {
		patch_addr[i] = 0x90;
	}

	DWORD restored = 0;
	VirtualProtect(patch_addr, k_h1_rt_addref_patch_len, old_protect, &restored);
	FlushInstructionCache(GetCurrentProcess(), patch_addr, k_h1_rt_addref_patch_len);

	CONSOLE_LOG_INFO("halo1 RT addref NOP installed @ %p (halo1+0x%llX, %zu bytes)",
		patch_addr, (unsigned long long)k_h1_rt_addref_patch_rva, k_h1_rt_addref_patch_len);
}

// --- halo1.dll FUN_1800AE280 — named-RT registration --------------------------

static constexpr uintptr_t k_h1_register_named_rts_rva = 0xAE280;

void halo1_call_register_named_render_targets() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1_call_register_named_render_targets: halo1.dll not loaded");
		return;
	}

	using Fn = void(*)();
	auto fn = reinterpret_cast<Fn>(
		reinterpret_cast<uintptr_t>(mod) + k_h1_register_named_rts_rva);
	CONSOLE_LOG_INFO("halo1_call_register_named_render_targets: calling halo1+0x%llX (%p)",
		(unsigned long long)k_h1_register_named_rts_rva, (void*)fn);
	// SEH-wrap because AE280 may still trip on later iterations even with
	// DAT_182e3c090 stamped — registering RTs partially is still better
	// than not at all (reduces dangling-SRV count downstream).
	__try {
		fn();
		CONSOLE_LOG_INFO("halo1_call_register_named_render_targets: returned");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		CONSOLE_LOG_WARN("halo1_call_register_named_render_targets: SEH 0x%08lX inside AE280 — partial registration may have completed",
			GetExceptionCode());
	}
}

// --- halo1.dll FUN_1800AE410 entry spin-wait detour --------------------------

static constexpr uintptr_t k_h1_init_rts_rva                = 0xAE410;
static constexpr uintptr_t k_h1_dat_182e3c090_rva           = 0x2E3C090;
static constexpr ptrdiff_t k_h1_engine_state_rt_root_offset = 0x500;
static constexpr ptrdiff_t k_h1_rt_root_pool_a_offset       = 0xE8;

using InitRenderTargetsFn = void(*)();
static InitRenderTargetsFn g_h1_init_rts_orig   = nullptr;
static HMODULE             g_h1_init_rts_module = nullptr;

static void h1_init_render_targets_detour() {
	auto base = reinterpret_cast<uintptr_t>(g_h1_init_rts_module);
	auto** engine_state_global =
		reinterpret_cast<unsigned char**>(base + k_h1_dat_182e3c090_rva);

	// Bounded spin: 500 iterations × 10ms = 5s cap. Long enough for the
	// engine init thread to finish (typically <100ms in practice), short
	// enough to fail loudly if something is structurally wrong.
	constexpr int  k_max_iters = 500;
	constexpr DWORD k_sleep_ms = 10;

	int iters = 0;
	void* pool_a = nullptr;
	for (; iters < k_max_iters; iters++) {
		auto* engine_state = *engine_state_global;
		if (engine_state) {
			auto* rt_root = *reinterpret_cast<unsigned char**>(
				engine_state + k_h1_engine_state_rt_root_offset);
			if (rt_root) {
				pool_a = *reinterpret_cast<void**>(
					rt_root + k_h1_rt_root_pool_a_offset);
				if (pool_a) break;
			}
		}
		Sleep(k_sleep_ms);
	}

	if (pool_a) {
		CONSOLE_LOG_INFO("halo1 RT init spinwait: pool ready after %d iters (%dms), pool=%p",
			iters, iters * (int)k_sleep_ms, pool_a);
	} else {
		CONSOLE_LOG_WARN("halo1 RT init spinwait: TIMEOUT after %d iters (%dms) — "
			"engine_state=%p; original AV at halo1+0x22B81F likely to follow",
			iters, iters * (int)k_sleep_ms, (void*)*engine_state_global);
	}

	g_h1_init_rts_orig();
}

void halo1_install_init_render_targets_spinwait() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo1) return;

	if (g_h1_init_rts_orig != nullptr) return;  // idempotent

	HMODULE mod = GetModuleHandleW(L"halo1.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo1 RT init spinwait: halo1.dll not loaded");
		return;
	}
	g_h1_init_rts_module = mod;

	auto* target = reinterpret_cast<void*>(
		reinterpret_cast<uintptr_t>(mod) + k_h1_init_rts_rva);

	if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
		// CreateHook will fail below if MH isn't ready; let it report.
	}

	void* trampoline = nullptr;
	auto st = MH_CreateHook(target,
		reinterpret_cast<void*>(&h1_init_render_targets_detour),
		&trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo1 RT init spinwait: MH_CreateHook failed (status=%d) @ %p",
			(int)st, target);
		return;
	}
	g_h1_init_rts_orig = reinterpret_cast<InitRenderTargetsFn>(trampoline);

	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo1 RT init spinwait: MH_EnableHook failed (status=%d) @ %p",
			(int)st, target);
		return;
	}

	CONSOLE_LOG_INFO("halo1 RT init spinwait installed @ %p (halo1+0x%llX)",
		target, (unsigned long long)k_h1_init_rts_rva);
}

