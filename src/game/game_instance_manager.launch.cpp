#include "game_instance_manager.h"

#include "halo1_native_overrides.h"
#include "halo2_d3d11_trace.h"
#include "halo4_native_overrides.h"
#include "haloreach_native_overrides.h"
#include "../network/engine_context_shim.h"
#include "../network/engine_context_logger.h"
#include "../network/mp_session_inject.h"
#include "../logging/logging.h"
#include "../player/player_manager.h"
#include "../rasterizer/rasterizer.h"
#include "../ui/ui_launch.h"

#include <thread>
#include <Windows.h>

using namespace libmcc;

// SEH-wrap a launch step so an AV inside the game DLL is captured + logged
// instead of taking the launcher down with no dump. Returns true on success.
template <typename Fn>
static bool seh_call(const char* name, Fn&& fn) {
	__try {
		fn();
		return true;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		DWORD code = GetExceptionCode();
		CONSOLE_LOG_ERROR("launch step '%s' raised SEH 0x%08lX — DLL likely AV'd in this call. "
			"Earlier log lines indicate which step we got to.",
			name, code);
		console_logger()->flush();
		return false;
	}
}

int c_game_instance_manager::launch_game_internal() {
	FARPROC func;
	t_expf_create_game_engine create_game_engine;
	t_expf_set_library_settings set_library_settings;
	s_language_settings language_settings;

	auto info = m_game_module_infos + m_game;
	auto device = rasterizer()->get_device();
	auto context = rasterizer()->get_device_context();
	auto swapchain = rasterizer()->get_swap_chain();

	const char* game_name = (m_game >= 0 && m_game < k_game_count) ? k_game_names[m_game] : "?";
	CONSOLE_LOG_INFO("launch_game_internal: module=%d (%s)", (int)m_game, game_name);
	halox::ui::ui_launch_set_status("Loading %s — preparing engine…", game_name);

	if (m_game_thread) {
		CONSOLE_LOG_WARN("launch_game_internal: a game is already running");
		return 1;
	}

	if (info->module_handle == nullptr) {
		CONSOLE_LOG_ERROR("launch_game_internal: module_handle is null (DLL not loaded)");
		return -1;
	}

	func = GetProcAddress(info->module_handle, EXPORT_FUNCTION_SET_LIBRARY_SETTINGS);
	if (func == nullptr) {
		CONSOLE_LOG_ERROR("launch_game_internal: missing %s export", EXPORT_FUNCTION_SET_LIBRARY_SETTINGS);
		return -2;
	}

	set_library_settings = reinterpret_cast<t_expf_set_library_settings>(func);

	wcscpy(language_settings.audio, k_languages[_language_en_US]);
	wcscpy(language_settings.text_1, k_languages[_language_en_US]);
	wcscpy(language_settings.text_2, k_languages[_language_en_US]);

	// Halo3 / Halo4 / etc. return errno_t (0 = success). Halo1 / Halo2 / older
	// MCC ports use a different ABI for SetLibrarySettings — they appear to
	// return a pointer (or intptr_t) where the low 32 bits look like a heap
	// address. Treating any non-zero return as an error there incorrectly
	// bails out before launch. Log the value but only fail on a known error
	// (negative-as-int = top-bit-set = clearly an HRESULT-style error).
	CONSOLE_LOG_INFO("launch_game_internal: calling set_library_settings");
	halox::ui::ui_launch_set_status("Loading %s — set_library_settings", game_name);
	decltype(set_library_settings(&language_settings)) set_library_rc = 0;
	if (!seh_call("set_library_settings", [&]{ set_library_rc = set_library_settings(&language_settings); })) {
		return -10;
	}
	if (auto rc = set_library_rc) {
		// Older MCC ports (halo1/halo2) return a pointer (intptr_t) where the
		// truncated low 32 bits look like a small or large signed int — could
		// be positive OR negative depending on which heap region the loader
		// happened to allocate in. The newer halo3+ ABI returns errno_t with
		// 0=success and a small positive int on failure. Treat only small
		// positive ints as real errors; everything else is a returned pointer.
		uint32_t urc = static_cast<uint32_t>(rc);
		if (urc > 0 && urc < 0x10000) {
			CONSOLE_LOG_ERROR("launch_game_internal: set_library_settings rc=%d (treating as error)", (int)rc);
			return -3;
		}
		CONSOLE_LOG_WARN("launch_game_internal: set_library_settings rc=0x%llX — looks like a returned pointer (older-ABI game), proceeding", (unsigned long long)(uintptr_t)(intptr_t)rc);
	}

	func = GetProcAddress(info->module_handle, EXPORT_FUNCTION_CREATE_GAME_ENGINE);
	if (func == nullptr) {
		CONSOLE_LOG_ERROR("launch_game_internal: missing %s export", EXPORT_FUNCTION_CREATE_GAME_ENGINE);
		return -4;
	}

	create_game_engine = reinterpret_cast<t_expf_create_game_engine>(func);

	// halo1's CreateGameEngine internals dereference *(halo1+0x2E3C090+0x118),
	// a render-config struct pointer. The proper population happens during
	// PreloadCommonBegin → InitGraphics → initialize_game's preload chain,
	// but our stub keeps the field non-null with sane width/height as a
	// belt-and-suspenders against any code path that reads before init.
	if (m_game == _module_halo1) {
		halo1_install_render_config_stub();
	}

	CONSOLE_LOG_INFO("launch_game_internal: calling create_game_engine");
	halox::ui::ui_launch_set_status("Loading %s — create_game_engine", game_name);
	if (!seh_call("create_game_engine", [&]{ create_game_engine(&m_game_engine); })) {
		return -11;
	}

	if (m_game_engine == nullptr) {
		CONSOLE_LOG_ERROR("launch_game_internal: create_game_engine returned null");
		return -5;
	}

	// CRITICAL ORDERING for halo1: slot 1 (InitGraphics / `initialize`) MUST
	// run BEFORE slot 4 (PreloadCommonBegin). MCC's launcher does this in
	// CoreSceneStateMachine_Update (mcc+0x1EFF34) — state 9→10 fires slot 1
	// with (device, ctx, swap), and slot 4 only fires later in a separate
	// per-game configure step. Slot 1 internally calls
	// FUN_1801EEA10(engine_config_struct), which sets the global
	// DAT_182e3bdd8 = engine_config_struct. That global is read by every
	// downstream init step, including FUN_1800AE280 (named-RT registration)
	// and FUN_1800AE410 (InitializeRenderTargets) — both AV on the
	// `cmp [r8+0x226], 0` instruction at AE2A2 / AE635 if it's NULL.
	//
	// HaloX previously called slot 4 first (because halo3+ has slot 4
	// stubbed and we matched that pattern), which left DAT_182e3bdd8 NULL
	// when initialize_game spawned the game thread → game thread AV'd
	// inside InitializeRenderTargets. Reversing the order is the proper
	// fix; the runtime byte-NOP at AE635 remains as a belt-and-suspenders.
	CONSOLE_LOG_INFO("launch_game_internal: calling game_engine->initialize");
	halox::ui::ui_launch_set_status("Loading %s — engine init (D3D)", game_name);
	// halo1's slot 1 (InitGraphics) reads its 4th arg as a swapchain via
	// `(*param_4)[+0x60]()` (IDXGISwapChain::GetDesc). BCS's launcher passes
	// swapchain twice; halo3+ ignored arg 4 so HaloX's nullptr worked there.
	// Pass swapchain again as the 4th arg specifically for halo1 to avoid a
	// null-vmethod call inside InitGraphics.
	// halo1's slot 1 (InitGraphics) reads its 4th arg as a swapchain via
	// `(*param_4)[+0x60]()` (IDXGISwapChain::GetDesc). BCS's launcher passes
	// swapchain twice; halo3+ ignored arg 4 so HaloX's nullptr worked there.
	// halo2 also tolerates nullptr — when given a real swapchain it
	// "claims" it as the engine's primary render target and produces an
	// infinite-mirror effect inside our ImGui game view (it writes to the
	// same backbuffer we composite into). Keep arg 4 = nullptr for halo2.
	auto* swapchain4 = (m_game == _module_halo1) ? swapchain : nullptr;
	if (!seh_call("game_engine->initialize", [&]{ m_game_engine->initialize(device, context, swapchain, swapchain4); })) {
		return -12;
	}

	// Secondary engine init for halo2 → groundhog (Anniversary visuals).
	//
	// halo2.dll handles the classic renderer + HUD; groundhog.dll is the H2A
	// renderer that handles the Anniversary world view. MCC's launcher
	// initializes BOTH when launching halo2 so the user can toggle visuals
	// at runtime (H key in our config). halox previously only loaded
	// groundhog's DLL (to read its data_access table for the multiplayer
	// browser) and never called create_game_engine / initialize on it —
	// resulting in halo2 toggling to Anniversary but seeing a black world
	// because groundhog's render subsystem was never wired to the d3d11
	// device.
	//
	// We mirror the primary engine's set_library_settings + create_game_engine
	// + initialize(device, ctx, swap, nullptr) sequence on groundhog. We do
	// NOT call initialize_game on it — only halo2 owns the game thread.
	if (m_game == _module_halo2) {
		auto* g_info = m_game_module_infos + _module_groundhog;
		if (g_info->module_handle == nullptr) {
			CONSOLE_LOG_WARN("launch_game_internal: groundhog module not loaded — Anniversary visuals will be blank");
		} else {
			FARPROC g_func = GetProcAddress(g_info->module_handle, EXPORT_FUNCTION_SET_LIBRARY_SETTINGS);
			if (g_func) {
				auto g_set_library_settings = reinterpret_cast<t_expf_set_library_settings>(g_func);
				CONSOLE_LOG_INFO("launch_game_internal: secondary (groundhog) set_library_settings");
				seh_call("groundhog set_library_settings", [&]{
					g_set_library_settings(&language_settings);  // ignore return code (older-ABI ptr)
				});
			}
			g_func = GetProcAddress(g_info->module_handle, EXPORT_FUNCTION_CREATE_GAME_ENGINE);
			if (g_func == nullptr) {
				CONSOLE_LOG_WARN("launch_game_internal: groundhog missing %s export — skipping secondary engine init",
					EXPORT_FUNCTION_CREATE_GAME_ENGINE);
			} else {
				auto g_create_game_engine = reinterpret_cast<t_expf_create_game_engine>(g_func);
				CONSOLE_LOG_INFO("launch_game_internal: secondary (groundhog) create_game_engine");
				if (seh_call("groundhog create_game_engine", [&]{
						g_create_game_engine(&m_game_engine_secondary);
					})
				    && m_game_engine_secondary != nullptr)
				{
					CONSOLE_LOG_INFO("launch_game_internal: secondary (groundhog) engine=%p — calling initialize",
						m_game_engine_secondary);
					seh_call("groundhog engine->initialize", [&]{
						// Same shape as primary; nullptr 4th arg matches halo2/halo3+
						// behavior (groundhog inherits halo3-style ABI).
						m_game_engine_secondary->initialize(device, context, swapchain, nullptr);
					});
					CONSOLE_LOG_INFO("launch_game_internal: secondary (groundhog) initialize returned");
				} else {
					CONSOLE_LOG_WARN("launch_game_internal: groundhog create_game_engine failed or returned null — Anniversary will be blank");
				}
			}
		}
	}

	// Slot 4 (PreloadCommonBegin) and slot 5 (PreloadLevelBegin) run AFTER
	// slot 1 — they read the DAT_182e3bdd8 global that slot 1 just
	// populated. Slot 5 takes a halo1-internal map index (not the libmcc
	// map_id enum); -1 short-circuits the per-map preload cleanly.
	//
	// BCS's MCC launcher fires these for halo2 too — without them the halo2
	// per-frame subsystem walker (halo2+0x263CAE) iterates objects with
	// uninitialized D3D resources at +0xD58 and AVs in d3d11 ~8s into a
	// campaign launch. The same vtable slot indices apply: slot 4 takes
	// (engine, device), slot 5 takes (engine, map_index).
	if (m_game == _module_halo1 || m_game == _module_halo2) {
		CONSOLE_LOG_INFO("launch_game_internal: calling %s PreloadCommonBegin (vtable slot 4)",
			k_game_names[m_game]);
		if (!seh_call("PreloadCommonBegin", [&]{
			halo1_call_preload_common_begin(m_game_engine, device);
		})) {
			return -14;
		}
		CONSOLE_LOG_INFO("launch_game_internal: calling %s PreloadLevelBegin (vtable slot 5)",
			k_game_names[m_game]);
		if (!seh_call("PreloadLevelBegin", [&]{
			halo1_call_preload_level_begin(m_game_engine, -1);
		})) {
			return -15;
		}
	}

	// halo2: install instrumentation hook on FUN_180103290 — the per-object
	// render handler that AVs at +0x45 when *(this+0xd58) is null/stale.
	// PreloadCommonBegin/PreloadLevelBegin (above) populate most objects'
	// d3d resources, but a few classes still slip through. The hook logs
	// each call's pointer-chain validity, suppresses unsafe calls (mimics
	// the function's tail-effect counter writes), and keeps the launch
	// progressing so we can find the next downstream issue. See
	// halo2_d3d11_trace.cpp.
	// 2026-05-01: trace is now disabled.  It logged + SUPPRESSED FUN_180103290
	// calls when *(p2+0xd58) looked stale, which protected against the
	// original launch-time AV at d3d11.dll+0x1059C5.  But the suppression
	// path NOPs out a real d3d11 draw — and Anniversary graphics cycles
	// objects through that vtable far more aggressively than classic, so
	// some frames had legitimate draws killed → stutter + black flicker.
	// The launch-time AV has been worked around elsewhere; leaving the
	// trace on is now a net negative.
	// 2026-05-01: trace turned off. Log-only diagnostic captured 944 calls
	// across classic→anniversary→classic, all with valid pointer chains —
	// disproving the +0xd58 hypothesis for the Anniversary world-render
	// failure. The bug is elsewhere; trace is just log noise now.
	if (false && m_game == _module_halo2) {
		halo2_d3d11_trace_install();
	}

	// halo1: stamp DAT_182e3bdd8 with a halox-built engine-context replica
	// (snooped live from MCC, see halo1_native_overrides.cpp). This populates
	// the descriptor at +0x118 with valid (width, height) — without it,
	// FUN_1801fcd40 reads the +0x24 "uninitialized" sentinel 0xFFFFFFFF and
	// IDIVs at halo1+0x1FCDD9. Defensive: only stamps if the global is null
	// or its existing descriptor is bad, so it never clobbers a successful
	// slot-1 init. The detour below is a belt-and-suspenders for callers
	// passing the bad sentinels through indirect paths the stamp doesn't
	// cover.
	if (m_game == _module_halo1) {
		halo1_install_engine_ctx_stub();
		halo1_install_div_zero_fix();
		// 2026-04-30: pre-calling halo1+0xAE280 (RegisterNamedRenderTargets)
		// reliably AVs inside FUN_1801f6fe0 (RT-array bubble-sort) at +0x1F700B.
		// SEH catches it but halo1's RT manager is left in a half-sorted /
		// half-initialized state — the next d3d11 commands from the game
		// thread then crash inside d3d11.dll (~+0x10074D) because they read
		// resources that never finished allocating. The RT vtable swap
		// poller below redirects GetSubResource / GetMipSubResource to our
		// stub for the symptoms this pre-call was guarding against, so
		// skipping it lets halo1's natural init flow complete the sort
		// without a torn pre-pass. If a later regression brings back the
		// d3d11+0x1155E4 hazard-check AV, we'll need a different RT
		// registration path that doesn't use FUN_1800AE280's sort.
		// halo1_call_register_named_render_targets();  // intentionally disabled
	}

	// halo1: TWO complementary fixes for races/NULLs in FUN_1800AE410:
	//
	// (1) spin-wait at FUN_1800AE410 entry until the engine main RT root's
	// +0xe8 pool A pointer is non-NULL. Live debugging (2026-04-28) showed
	// the pointer is stamped by a sibling init thread; on fast machines
	// the foreground races past it and AVs at halo1+0x22B81F inside
	// vt[0x1B] = FUN_18022b7b0 with NULL pool. Spin-wait holds entry until
	// the sibling stamps it (cap 5s).
	//
	// (2) byte-NOP at halo1+0xAE635 — covers a SEPARATE NULL deref:
	// FUN_1800AE410+0x214 calls vt[0x1A] = FUN_18022b5d0 (GetSubResource)
	// which returns NULL even after the spin-wait (likely another sub-init
	// race that's harder to gate on a single global). NOPing the
	// `mov rdx, [rax]; call [rdx+8]` AddRef pair lets the function
	// continue with NULL stored in DAT_181b85e80[i] — the engine's later
	// consumers either re-fetch via vt[0xD8] (now safe with spin-wait) or
	// check for NULL.
	//
	// Together these get past the original 0x22B81F and 0xAE635 crashes;
	// the next downstream issue (D3D11 NULL-resource AV at d3d11+0x107593)
	// is a different problem documented in
	// memory/project_halo1_render_config_crash.md.
	if (m_game == _module_halo1) {
		halo1_install_init_render_targets_spinwait();
		// 2026-05-01: AddRef NOP at +0xAE635 disabled. The NOP was originally
		// added because AddRef crashed on the engine's bad/NULL subresource
		// returns. Now that our stubs always return real ID3D11* COM objects
		// (and the spinwait ensures the orig-chain path also returns valid
		// objects), the engine's natural per-store AddRef must run — without
		// it, the engine stores N pointers without bumping refcount, then
		// the destructor walks the table calling Release per entry and
		// over-releases, AVing inside d3d11 (#0=heap, #1=d3d11+0xBD32,
		// #2=halo1+0x22B24E in FUN_18022b0a0). See
		// memory/project_halo1_spinwait_nop_validated.md for the full chain.
		// halo1_install_rt_subresource_addref_nop();
		// Path-B fix for the d3d11+0x10074D crash: create a real
		// ID3D11Texture2D so the swap stubs return a valid d3d11 COM
		// object instead of our fake C++ struct. Live RE in MCC showed
		// halo1's normal init writes real ID3D11Resource* into these
		// tables; d3d11's command processor walks them assuming real
		// vtables — anything else AVs deep inside d3d11.
		halo1_init_stub_d3d11_resource(device);
		// 2026-04-30 evening: stub_rt_get_subresource / _mip_subresource
		// now read the real *(self+0xE8) first and only fall back to the
		// stub SRV/RTV when that pointer is NULL/junk. Real RTs render
		// real; only the partially-init RTs (which would AV at +0x22B81F
		// or +0xAE635 without a stub) get our 4x4 dummy.
		halo1_start_rt_vtable_swap_poller();
		// 2026-04-30: fire halo1's "SYS::haloInit" subsystem initializer
		// (FUN_180AC2528 → FUN_180AAE97C) that allocates the engine-state
		// block at DAT_182D91330 and runs Session command handlers + ~30
		// other inits. Without it the game thread halts at halo1+0xAD7D58
		// (0xBEEF0117) the moment it checks `*DAT_182D91330 != 0`. MCC
		// drives this via a tag dispatcher; halox calls it directly.
		// Must run BEFORE initialize_game (which sets DAT_182E3B9A0 and
		// arms the halt precondition).
		if (seh_call("halo1_call_engine_subsystem_init", [&]{
			halo1_call_engine_subsystem_init();
		})) {
			CONSOLE_LOG_INFO("launch_game_internal: ran halo1 SYS::haloInit subsystem init");
		} else {
			CONSOLE_LOG_WARN("launch_game_internal: halo1 SYS::haloInit subsystem init SEH'd — proceeding anyway");
		}
	}

	// Reach campaign: install the FUN_1802a41c4 (scenario-resolve+load) hook
	// BEFORE initialize_game spawns the engine thread, then arm the one-shot
	// run_flag override. PPF's first call to FUN_1802a41c4 will then take
	// the run_flag=1 branch and queue the actual map load via FUN_180022058.
	// Documented in memory/project_haloreach_campaign_launch.md.
	if (m_game == _module_haloreach) {
		haloreach_install_campaign_load_hook();
		haloreach_start_state_logger();
		if (m_game_options_storage.game_options.game_mode == _game_mode_campaign) {
			haloreach_arm_campaign_load_force();
		}
	}

	// halo1: install the engine_context logging shim BEFORE initialize_game so
	// every vtable slot halo1 dispatches on g_game_manager (its substitute for
	// MCC's c_engine_context @ MCC+0x3F7B190) gets logged. Output to halox.log
	// as `ec_log: slot[N/0xN] ...` lines — grep after a launch run to enumerate
	// the actual implementation surface (likely 15-30 slots, not all ~190).
	// See project_halo1_engine_ctx_snoop.md for the full rationale.
	// Mutually exclusive with engine_context_shim's send/recv override; only
	// engaged for halo1 because haloreach already uses the existing shim.
	if (m_game == _module_halo1) {
		halox::network::engine_context_logger_install();
	}

	// halo4: guard FUN_1800785CC against null arg from the shared-scenario
	// preload state machine. Caller (FUN_18003E230, case g_state == 1) passes
	// a vfunc result that is null/unmapped because halox doesn't reproduce the
	// MCC-side shared-tag preload. AV at halo4+0x78640 on `movups [rdi+0x10]`.
	// The hook returns 0 on null/SEH; caller already handles 0 as a safe-exit.
	if (m_game == _module_halo4) {
		halo4_install_resource_register_guard();
	}

	CONSOLE_LOG_INFO("launch_game_internal: calling game_engine->initialize_game");
	halox::ui::ui_launch_set_status("Loading %s — initialize_game (loading scenario)", game_name);
	if (!seh_call("game_engine->initialize_game", [&]{
		m_game_thread = m_game_engine->initialize_game(
			&g_game_manager,
			&m_game_options_storage.game_options);
	})) {
		return -13;
	}

	if (m_game_thread == nullptr) {
		auto err = GetLastError();
		CONSOLE_LOG_ERROR("launch_game_internal: initialize_game returned null thread (GetLastError=%lu)", err);
		return err;
	}

	CONSOLE_LOG_INFO("launch_game_internal: game thread started @ 0x%llX", (uint64_t)m_game_thread);
	halox::ui::ui_launch_set_status("%s — game thread running", game_name);

	// MCC's CoreSceneStateMachine_Update (mcc+0x1EFF34, state 11) immediately
	// follows initialize_game with `engine->post_message(2, NULL)` and keeps
	// firing it per frame as long as DAT_144000b9a != 0. halo1's vtable slot 3
	// (post_message @ +0x93CE0) takes the generic path for msg_id=2: pop a
	// free node from engine_ctx+0x440, stash msg_id, push to +0x430 — feeds
	// the engine's event SLIST that the game thread drains. halox previously
	// never sent this so the engine sat in its loaded-but-not-driving state.
	// Send it once here as the kickoff; per-frame ticking is layer-2 work
	// (would live alongside c_game_manager::begin_frame on the game thread).
	if (m_game == _module_halo1) {
		if (seh_call("post_message(halo1, msg_id=2)", [&]{
			m_game_engine->post_message((libmcc::e_game_message)2, nullptr);
		})) {
			CONSOLE_LOG_INFO("launch_game_internal: posted halo1 kickoff message (msg_id=2)");
		}
	}

	// Engine-context shim — patches g_game_manager's vtable in-place so
	// haloreach's `(*g_engineContext)+0x148` (send) and `+0x158` (recv) route
	// through halox's own UDP transport instead of MCC's networking stack.
	// Driven by env vars HALOX_BIND_PORT (default 27015) and HALOX_PEER_ADDR
	// + HALOX_PEER_PORT (no default — sends drop until set). Reach only
	// indirects through these slots when peer.type == 8 (networked) and the
	// session has a populated peer table at +0x10 — neither happens in the
	// solo flow today, so this swap is a no-op until layer-2 work drives
	// reach into MP and injects a peer.
	if (m_game == _module_haloreach) {
		char bind_port_buf[32]   = {};
		char peer_addr_buf[64]   = {};
		char peer_port_buf[32]   = {};
		DWORD got_bp = GetEnvironmentVariableA("HALOX_BIND_PORT", bind_port_buf, sizeof(bind_port_buf));
		DWORD got_pa = GetEnvironmentVariableA("HALOX_PEER_ADDR", peer_addr_buf, sizeof(peer_addr_buf));
		DWORD got_pp = GetEnvironmentVariableA("HALOX_PEER_PORT", peer_port_buf, sizeof(peer_port_buf));
		int bind_port = (got_bp > 0 && got_bp < sizeof(bind_port_buf)) ? atoi(bind_port_buf) : 27015;
		int peer_port = (got_pp > 0 && got_pp < sizeof(peer_port_buf)) ? atoi(peer_port_buf) : 0;
		const char* peer_addr = (got_pa > 0 && got_pa < sizeof(peer_addr_buf)) ? peer_addr_buf : nullptr;
		halox::network::engine_context_shim_init(bind_port, peer_addr, peer_port);

		// Layer 2 — MP-session injection. Opt-in via HALOX_MP_INJECT=1 so it
		// doesn't fire on normal launches. Drives haloreach into MP state and
		// stamps a peer pointer into the recv-path 2-slot table so reach's
		// state machine routes 0→1→2→6 (MP post-join tick) and PollNetworkEvents
		// actually iterates a peer, exercising the engine_context_shim slots.
		// Recipe documented in memory/reference_haloreach_mp_session_layout.md.
		char mp_inject_buf[8] = {};
		DWORD got_mi = GetEnvironmentVariableA("HALOX_MP_INJECT", mp_inject_buf, sizeof(mp_inject_buf));
		char mp_real_buf[8] = {};
		DWORD got_mr = GetEnvironmentVariableA("HALOX_MP_INJECT_REAL", mp_real_buf, sizeof(mp_real_buf));

		uint64_t peer_guid = 0xC0DEC0DE00000000ull | (uint64_t)bind_port;

		if (got_mr > 0 && mp_real_buf[0] == '1') {
			// REAL path — call Reach's actual session-init pipeline. No no-op
			// hooks, no zero buffer. is_host flag determined by HALOX_PEER_ADDR
			// presence: a host has no preset peer (clients connect to it);
			// a client has a peer addr set by the Session Browser join flow.
			bool is_host = (got_pa <= 0);
			halox::network::mp_session_inject_real_apply(peer_guid, is_host);
		} else if (got_mi > 0 && mp_inject_buf[0] == '1') {
			// LEGACY stub path — zero buffer + 8 no-op cocktail. Boots without
			// crashing but never renders or sends packets. Kept as a fallback
			// so we can A/B compare against the real path.
			halox::network::mp_session_inject_apply(peer_guid);
			halox::network::mp_session_inject_start_pinning_thread();
		}
	}

	// One-shot watchdog: poll the game thread for the first 30 seconds and
	// report if it exits silently or hangs. Reach (and any newly-loaded game)
	// may crash inside its own thread without ever invoking our restart_game
	// callback, in which case the launcher just goes quiet — this gives us
	// "exited with code X at +Ns" logging instead.
	HANDLE watch_handle = nullptr;
	if (DuplicateHandle(GetCurrentProcess(), m_game_thread,
	                    GetCurrentProcess(), &watch_handle,
	                    0, FALSE, DUPLICATE_SAME_ACCESS) && watch_handle) {
		auto game = m_game;
		std::thread([watch_handle, game]() {
			const char* name = (game >= 0 && game < k_game_count) ? k_game_names[game] : "?";
			for (int sec = 1; sec <= 30; ++sec) {
				DWORD wait = WaitForSingleObject(watch_handle, 1000);
				if (wait == WAIT_OBJECT_0) {
					DWORD code = 0;
					GetExitCodeThread(watch_handle, &code);
					CONSOLE_LOG_WARN(
						"launch watchdog: %s game thread exited at +%ds (exit code 0x%lX / %ld)",
						name, sec, code, (long)code);
					CloseHandle(watch_handle);
					return;
				}
			}
			CONSOLE_LOG_INFO("launch watchdog: %s game thread still running after 30s", name);
			CloseHandle(watch_handle);
		}).detach();
	}

	return 0;
}

int c_game_instance_manager::launch_game(const s_game_prop* prop) {
	m_game = prop->module;

	CONSOLE_LOG_INFO(
		"launch_game: module=%d (%s) mode=%d map=%d difficulty=%d",
		(int)prop->module,
		(prop->module >= 0 && prop->module < k_game_count) ? k_game_names[prop->module] : "invalid",
		(int)prop->mode,
		(int)prop->map.builtin_map_id,
		(int)prop->difficulty);

	auto info = m_game_module_infos + m_game;
	auto game_options = &m_game_options_storage.game_options;
	i_unknown_ptr<i_scenario_map_variant> map_variant = nullptr;
	i_unknown_ptr<i_game_engine_variant> game_variant = nullptr;

	auto data_access = get_module_data_access(prop->module);

	if (data_access == nullptr) {
		CONSOLE_LOG_ERROR(
			"launch_game: data_access is null for %s (module_handle=%p, error_code=%lu) — "
			"the DLL didn't load. Run halox.exe from a directory containing %s/%s.dll.",
			(prop->module >= 0 && prop->module < k_game_count) ? k_game_names[prop->module] : "?",
			info->module_handle, info->error_code,
			(prop->module >= 0 && prop->module < k_game_count) ? k_game_names[prop->module] : "?",
			(prop->module >= 0 && prop->module < k_game_count) ? k_game_names[prop->module] : "?");
		return -1;
	}

	switch (prop->mode) {
	case _game_mode_campaign:
		if (prop->module == _module_groundhog) {
			CONSOLE_LOG_ERROR("launch_game: groundhog has no campaign mode");
			return -2;
		}
		new (&m_game_options_storage.campaign_game_options) c_campaign_game_options();
		game_options->difficulty_level = prop->difficulty;
		// Skull bitmask + insertion point come from the launcher UI. The
		// libmcc s_flags<uint64_t,e_skull>::bit_set helper uses (1 << pos)
		// internally which overflows past bit 31, so stamp the raw uint64_t
		// via the operator=(T) overload — it sets the underlying `n` field
		// directly without bit-position arithmetic.
		game_options->skulls = prop->skull_flags;
		game_options->campaign_insertion_point = prop->campaign_insertion_point;
		// Reach's ProcessSessionJoinState (haloreach.dll+0xDF18) drives the
		// launch state machine off DAT_1824971ac (game_options+0xC = game_mode):
		//   game_mode==1 (campaign) → DAT_180c1a114 = 4 (ProcessPlaybackFrame)
		//   game_mode==3 (multiplayer) → DAT_180c1a114 = 6 (MP post-join tick)
		//   game_mode==5 (firefight) → DAT_180c1a114 = 7 (firefight tick)
		// The 60s individual-state and 90s overall timeouts in
		// ExternalLaunchStateMachineTick (haloreach.dll+0xEEA4) are what fire
		// `restart_game(reason=4, "external_launch_..._timeout")` — the user-
		// observed "no map selected" string is from the multiplayer LOBBY
		// (TickInGameState/GetPendingLobbyId) and only appears if the launch
		// is wrongly routed through MP. The c_campaign_game_options ctor now
		// clears the parent's _game_options_flags_multiplayer bit so Reach's
		// initialize_game (Session_LoadJoinDescriptor) does NOT bind a host
		// address and the launch takes the campaign branch.
		//
		// Reach uses INTERNAL scenario IDs (not the libmcc map_id enum) — the
		// translation lives in haloreach.dll's data_access but is not exposed
		// via the libmcc i_data_access abstraction. Until a Reach-specific
		// resolver is wired in, legacy_map_id ends up as the libmcc enum
		// stamped by the post-switch block, which Reach's data_access lookup
		// rejects. The fixes here at minimum keep MP from being misrouted and
		// stop the lobby-side "no map selected" state — campaign load still
		// requires the internal-id translation, tracked in
		// project_haloreach_campaign_launch.md.
		// Reach-specific: zero host_address AND clear the multiplayer flag
		// (inherited from c_game_options ctor) to keep campaign out of Reach's
		// MP/lobby flow. halo2 / halo3 / etc. campaigns hang at
		// set_game_state:0 if the MP flag is cleared, so only Reach gets the
		// flag clear, and only Reach gets host_address=0 (the post-switch
		// block writes 123 for everything else).
		if (prop->module == _module_haloreach) {
			// MP flag CLEAR + host=0 keeps the lobby out of the launch path.
			// Empirically (state-logger run 2026-04-28):
			//   MP set   → PPF picks uVar4=10 → state 10 (ProcessSessionNetworkTick)
			//              → "no map selected" from lobby code
			//   MP clear → PPF picks uVar4=9 → state 9 (Session_JoinUpdateTick)
			//              → terminal state 11 → black screen + nvwgf2umx AV
			// Neither path populates the scenario because PPF calls
			// FUN_1802a41c4(GUID, map_id, run_flag=0) — preflight only, no
			// actual map-load queue. The hook in haloreach_native_overrides
			// forces run_flag=1 so the load actually queues. Lobby misroute
			// is the worse failure (immediate timeout vs deferred AV), so we
			// keep MP cleared.
			game_options->host_address = 0;
			game_options->flags.bit_set(_game_options_flags_multiplayer, false);
		}
		// halo2 campaign: explicit insertion_point=0. Note: there is NO
		// campaign-specific game_variant file under hopper_game_variants —
		// halo2's campaign gametype is internal to the engine. Stamping an
		// MP variant blob (e.g. 01_slayer) into game_options.game_variant
		// for a campaign launch was tried and made things worse — the
		// engine reads game_variant.gametype and would interpret it as
		// "MP rules on a campaign map," wrong-pathing the per-frame
		// subsystem walker.
		// (halo2 hardcoded `campaign_insertion_point = 0` removed — the user-
		// supplied value via the launcher prop now flows through. Defaults to
		// 0 so the original behavior is preserved when the user doesn't pick.)
		// halo4 campaign: dispatcher reads from custom_campaign_map_id, NOT
		// map_id (per Round 3 RE of haloreach Engine_DispatchQueuedCommand
		// — game_mode==1 branch reads custom_campaign_map_id and checks
		// flags == 0x8888 for builtin or 0x1111 for insertion-point format;
		// pattern is shared across engines per Round 4-6 cross-validation).
		// Without this stamp halo4 logs "GAME QUIT[4]: no map selected"
		// after 2s. The s_scenario_map_id default ctor already supplies
		// flags=0x8888 + part1=0 + part2=0 so a plain assignment works.
		// Other engines (halo2/halo3/halo3odst) currently rely on the same
		// implicit-fallback that's failing on halo4 — extend this stamp to
		// them if their campaigns also fail.
		if (prop->module == _module_halo4) {
			game_options->custom_campaign_map_id = prop->map;
		}
		break;
	case _game_mode_spartan_ops:
		if (prop->module != _module_halo4) {
			CONSOLE_LOG_ERROR("launch_game: spartan ops only supported by halo4");
			return -2;
		}
		new (&m_game_options_storage.spartan_ops_game_options) c_spartan_ops_game_options();
		break;
	case _game_mode_multiplayer:
		if (prop->module == _module_halo3odst) {
			CONSOLE_LOG_ERROR("launch_game: halo3odst has no multiplayer mode");
			return -2;
		}
		new (&m_game_options_storage.multiplayer_game_options) c_multiplayer_game_options();

		if (prop->hopper_game_variant == -1) {
			CONSOLE_LOG_ERROR(
				"launch_game: multiplayer requires a Game Variant — open the menu and pick one. "
				"None selected (hopper_game_variant=-1).");
			return -2;
		}
		game_variant = get_game_variant(prop->module, prop->hopper_game_variant);
		if (game_variant == nullptr) {
			CONSOLE_LOG_ERROR(
				"launch_game: failed to load game variant index %d for %s",
				prop->hopper_game_variant,
				k_game_names[prop->module]);
			return -2;
		}

		if (prop->module == _module_halo1 ||
			prop->module == _module_halo2 ||
			prop->module == _module_halo3odst) {
			map_variant = nullptr;
		} else {
#ifdef _DEBUG
			map_variant =
				i_unknown_ptr<i_scenario_map_variant>(data_access->create_scenario_map_variant_from_map_id(&prop->map));
#else
			if (prop->hopper_map_variant == -1) {
				CONSOLE_LOG_ERROR(
					"launch_game: multiplayer needs a Map Variant in release builds (none selected)");
				return -2;
			}
			map_variant = get_map_variant(prop->module, prop->hopper_map_variant);
#endif

			if (map_variant == nullptr) {
				CONSOLE_LOG_ERROR(
					"launch_game: map_variant creation failed (map_id=%d). "
					"Make sure the map id matches a real %s scenario.",
					(int)prop->map.builtin_map_id,
					k_game_names[prop->module]);
				return -2;
			}
		}

		if (game_variant) {
			game_variant->copy_to_game_options(game_options);
		}
		if (map_variant) {
			map_variant->copy_to_game_options(game_options);
		}
		break;
	case _game_mode_ui_shell:
		new (&m_game_options_storage.ui_shell_game_options) c_ui_shell_game_options();
		break;
	case _game_mode_firefight:
		if (!(prop->module == _module_halo3odst || prop->module == _module_haloreach)) {
			CONSOLE_LOG_ERROR("launch_game: firefight only supported by halo3odst and haloreach");
			return -2;
		}
		new (&m_game_options_storage.firefight_game_options) c_firefight_game_options();
		break;
	default:
		CONSOLE_LOG_ERROR("launch_game: unsupported mode %d", (int)prop->mode);
		return -2;
	}

	// set map id
	game_options->map_id = prop->map;

	if (game_options->map_id.is_builtin()) {
		game_options->legacy_map_id = game_options->map_id.builtin_map_id;
	}

	// Networked modes need a stub host_address so the local peer self-binds
	// as host. The host_address=0 carve-out is REACH-SPECIFIC: Reach's
	// Session_LoadJoinDescriptor reads (flags & 0x8) and the host_address
	// pointer to decide whether to enter the MP/lobby flow; for solo
	// campaign on Reach we need both cleared. halo2 / halo3 / halo3odst /
	// halo4 all expect host_address=123 for their own loader paths even in
	// campaign — zeroing it freezes halo2's campaign load.
	if (prop->mode != _game_mode_campaign || prop->module != _module_haloreach) {
		game_options->host_address = 123;
	}

	// set player options
	game_options->player_options.player_count = 1;
	game_options->player_options.peer_count = 1;

	game_options->player_options.address[0] = game_options->host_address;
	game_options->player_options.player_options[0].xuid = player_manager()->get_player_xuid(_local_player_0);
	game_options->player_options.player_options[0].address = game_options->host_address;

	if (!PostMessageW(g_win32_parameter.window_handle, _window_message_game_launch, 0, 0)) {
		return GetLastError();
	}

	return 0;
}
