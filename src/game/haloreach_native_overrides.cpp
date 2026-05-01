#include "haloreach_native_overrides.h"

#include "game_instance_manager.h"
#include "mcc_user_settings.h"
#include "../logging/logging.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <thread>

#include <MinHook.h>

using namespace libmcc;

// --- haloreach.dll cache layout (RE-confirmed via ReverseMe agent) ----------
// Cache: per-player profile cache, accessed by Camera_UpdateFOV (RVA 0xC8554),
// FUN_18026C204 (per-frame copier, RVA 0x26C204), and 5+ other readers. All
// index as &DAT_182A03C50 + i*0xAB8, bounded at i < 4.
//
// IMPORTANT: unlike halo3, Reach's KB/M binding tables are NOT in this cache.
// They live in a separate per-player input-context table at Ghidra VA
// 0x18287FF94 (stride 0x380, ~0x700 per slot), accessed via
// InputContext_GetBindingStruct (RVA 0xD1070, 114 callers). That table uses
// 16-byte binding structs, not a flat u16 array. Stamping bindings into Reach
// requires a separate writer keyed off that RVA — not implemented here.

static constexpr uintptr_t k_hr_cache_rva       = 0x2A03C50;  // CONFIRMED
static constexpr size_t    k_hr_cache_stride    = 0xAB8;       // CONFIRMED
static constexpr int       k_hr_cache_count     = 4;           // CONFIRMED

static constexpr size_t    k_hr_off_flags       = 0x00;        // CONFIRMED — bit 2 = valid
static constexpr uint8_t   k_hr_flags_valid_bit = 0x4;

static constexpr size_t    k_hr_off_fov_deg     = 0x8C;        // CONFIRMED — float, vertical FOV in degrees

// --- haloreach.dll KB binding override target (CONFIRMED) ------------------
//
// Three contiguous int32[81][2] tables in .data, mapping tag → polled-VK
// index. Total 0x798 bytes from 0x287F7E0..0x287FF78. NEVER re-initialized
// at runtime (zero WRITE xrefs in Ghidra), so launcher writes stick.
//
// Element value is NOT a Win32 VK — it's an index 0..0x67 into the engine's
// polled-VK list at RVA 0x98FDF0 (a 0x68-entry table of Win32 VKs that
// ProcessInputAndDispatchEvents (RVA 0x239E0) polls each frame via
// GetKeyState). So writing a binding requires:
//   1. Look up the user's Win32 VK in the polled list → get index
//   2. Write the index into kb_primary[tag*2 + slot]
// We snapshot the polled list once per module-load.

static constexpr uintptr_t k_hr_kb_primary_rva    = 0x287FCF0;    // int32[81][2] tag → polled-VK idx
static constexpr uintptr_t k_hr_kb_secondary_rva  = 0x287F7E0;    // int32[81][2] alt input channel
static constexpr uintptr_t k_hr_polled_vk_rva     = 0x98FDF0;     // u16[0x68] Win32 VK list
static constexpr int       k_hr_polled_vk_count   = 0x68;
static constexpr int       k_hr_table_tag_count   = 81;            // 0x00..0x50 (last 8 are axis sentinels)
static constexpr int32_t   k_hr_kb_unbound        = -1;            // 0xFFFFFFFF in the table

// libmcc::e_game_abstract_button → Reach internal tag (mostly LIKELY, some
// CONFIRMED — see project_haloreach_profile_bypass.md "Tag map" section for
// per-entry confidence).
static const int8_t k_libmcc_to_haloreach_tag[k_game_abstract_button_count] = {
	/* jump                            */  0x00, // LIVE-DUMP 2026-04-30 (was 0x01)
	/* switchgrenade                   */  0x23, // LIVE-DUMP 2026-04-30 (was 0x0B; tag 0x0B was unmapped → engine default coincided with user's jump key → "same button" symptom)
	/* actionreload                    */  -1,
	/* reload                          */  0x1E, // LIVE-DUMP 2026-04-30 (was 0x0C)
	/* switchweapon                    */  0x05, // LIKELY
	/* meleeattack                     */  0x06, // LIVE-DUMP 2026-04-30 (was 0x04; user re-reported melee/crouch inversion after the prior 0x03→0x04 swap — actual reach tag for melee is 0x06, not in either earlier guess)
	/* flashlight                      */  0x06, // CONFIRMED — but conflicts with melee above; flashlight may need re-RE if it stops working
	/* throwgrenade                    */  0x1F, // LIVE-DUMP 2026-04-30 (was 0x0A)
	/* fire                            */  0x07, // CONFIRMED
	/* crouch                          */  0x05, // LIVE-DUMP 2026-04-30 (was 0x03; same RE pass as melee)
	/* zoom                            */  0x06, // LIKELY (reach reuses flashlight)
	/* zoomin                          */  0x19, // LIKELY
	/* zoomout                         */  0x1A, // LIKELY
	/* swapweapon                      */  0x05, // LIKELY (single button)
	/* sprint                          */  0x16, // CONFIRMED
	/* bansheebomb                     */  -1,
	/* moveforward                     */  0x10, // CONFIRMED (axis)
	/* movebackward                    */  0x10, // CONFIRMED (axis)
	/* strafeleft                      */  0x10, // CONFIRMED (axis)
	/* straferight                     */  0x10, // CONFIRMED (axis)
	/* showscores                      */  0x18, // LIKELY
	/* primaryvehicletrick             */  -1,
	/* secondaryvehicletrick           */  -1,
	/* equipment                       */  0x35, // LIKELY (Reach AA / jet pack)
	/* secondaryfire                   */  0x0A, // LIKELY
	/* lifteditor                      */  0x2A, // CONFIRMED
	/* dropeditor                      */  -1,
	/* grabobjecteditor                */  0x20, // CONFIRMED
	/* boosteditor                     */  0x2A, // LIKELY
	/* croucheditor                    */  0x1F, // LIKELY
	/* deleteobjecteditor              */  -1,
	/* createobjecteditor              */  -1,
	/* opentoolmenueditor              */  0x23, // CONFIRMED
	/* switchplayermodeeditor          */  0x22, // CONFIRMED
	/* scopezoomeditor                 */  -1,
	/* playerlockformanipulationeditor */  0x24, // LIKELY
	/* showhidepanneltheater           */  -1,
	/* showhideinterfacetheater        */  -1,
	/* togglefirstthirdpersonviewtheater */ 0x2F, // CONFIRMED
	/* camerafocustheater              */  -1,
	/* fastforwardtheater              */  0x2B, // LIKELY
	/* fastrewindtheater               */  0x2B, // LIKELY (paired analog)
	/* stopcontinueplaybacktheater     */  0x2C, // CONFIRMED
	/* playbackspeeduptheater          */  0x2E, // LIKELY
	/* enterfreecameramodetheater      */  -1,
	/* movementspeeduptheater          */  -1,
	/* panningcameratheater            */  -1,
	/* cameramoveuptheater             */  -1,
	/* cameramovedowntheater           */  -1,
	/* dualwield                       */  0x3C, // LIKELY
	/* zoomcameratheater               */  -1,
	/* togglerotationaxeseditor        */  -1,
	/* duplicateobjecteditor           */  0x24, // LIKELY
	/* lockobjecteditor                */  -1,
	/* resetorientationeditor          */  -1,
	/* reloadsecondary                 */  0x0D, // LIKELY
	/* previousgrenade                 */  -1,   // not in Reach
	/* specialaction                   */  0x1D, // CONFIRMED
	/* loadoutmenu                     */  -1,
	/* activatewaypoint                */  0x1B, // LIKELY
	/* activatewaypointalt             */  0x1C, // LIKELY
	/* pingnavpoints                   */  0x1B, // LIKELY
	/* raisehornet                     */  -1,
	/* lowerhornet                     */  -1,
	/* flashlightalt                   */  0x38, // LIKELY
	/* nextgrenade                     */  -1,   // not in Reach
};
// Snapshot of the engine's polled-VK list. The array at DAT_18098FDF0 is
// uint8_t[0x68] — 104 entries, one byte per slot. Each slot holds a Win32 VK
// the engine polls via GetKeyState every frame. (Earlier I'd assumed u16 per
// slot, which made every lookup miss because real VKs only appeared as the
// low/high byte of artificial u16 pairs.)
static uint8_t g_polled_vks[k_hr_polled_vk_count] = {};
static HMODULE g_polled_vks_module = nullptr;

static bool g_logged_polled_dump = false;
static bool g_logged_writes = false;

static void refresh_polled_vk_list(HMODULE mod) {
	if (mod == g_polled_vks_module) return;
	const uint8_t* src = reinterpret_cast<const uint8_t*>(
		reinterpret_cast<uintptr_t>(mod) + k_hr_polled_vk_rva);
	for (int i = 0; i < k_hr_polled_vk_count; ++i) g_polled_vks[i] = src[i];
	g_polled_vks_module = mod;
	// Re-arm the per-launch diagnostic dumps so a relaunch shows fresh data.
	g_logged_polled_dump = false;
	g_logged_writes = false;
}

// MCC's ini stores side-specific VKs (VK_LCONTROL=0xA2, VK_RSHIFT=0xA1, etc.)
// but Reach's polled list almost certainly only contains the combined keys
// (VK_CONTROL=0x11, VK_SHIFT=0x10, VK_MENU=0x12). Map side keys → combined so
// "LCtrl bound to armor ability" still hits a polled VK.
static uint16_t normalize_vk(uint16_t vk) {
	switch (vk) {
	case 0xA0: case 0xA1: return 0x10;  // VK_LSHIFT/RSHIFT → VK_SHIFT
	case 0xA2: case 0xA3: return 0x11;  // VK_LCONTROL/RCONTROL → VK_CONTROL
	case 0xA4: case 0xA5: return 0x12;  // VK_LMENU/RMENU → VK_MENU
	default:              return vk;
	}
}

// Returns the polled-list index for a Win32 VK, or -1 if the engine doesn't
// poll that key by default. Tries the user's VK as-is first, then a
// normalized combined-key variant so side-specific VKs can still bind.
static int vk_to_polled_index(uint16_t vk) {
	if (vk == 0) return -1;
	uint8_t vk8 = (uint8_t)vk;  // VKs fit in 1 byte
	for (int i = 0; i < k_hr_polled_vk_count; ++i) {
		if (g_polled_vks[i] == vk8) return i;
	}
	uint8_t alt = (uint8_t)normalize_vk(vk);
	if (alt != vk8) {
		for (int i = 0; i < k_hr_polled_vk_count; ++i) {
			if (g_polled_vks[i] == alt) return i;
		}
	}
	return -1;
}

static void log_polled_vk_dump() {
	if (g_logged_polled_dump) return;
	g_logged_polled_dump = true;
	// Dump the engine's polled VK list once. Useful for diagnosing which
	// bindings silently skipped because the user's VK isn't in the list.
	char line[16 * 4] = {};
	for (int row = 0; row < (k_hr_polled_vk_count + 15) / 16; ++row) {
		line[0] = 0;
		char* p = line;
		int hi = row * 16 + 16;
		if (hi > k_hr_polled_vk_count) hi = k_hr_polled_vk_count;
		for (int i = row * 16; i < hi; ++i) {
			p += std::snprintf(p, 4, "%02X ", g_polled_vks[i]);
		}
		CONSOLE_LOG_INFO("Reach polled VKs [%02X..]: %s", row * 16, line);
	}
}

// --- Replication_UpdateEntityState detour ----------------------------------
//
// haloreach.dll + 0x2C8DFC rebuilds all three binding tables (kb-secondary,
// axis, kb-primary) from MCC's CustomKeyboardMouseMappingV2 list and memcpys
// them into 0x18287F7E0..0x18287FF90 in one shot. That memcpy is the ONLY
// writer to those tables (zero other write xrefs in Ghidra), so re-stamping
// our overrides immediately AFTER the trampoline-call gives us the last word.
//
// Only triggered on customization-sync (per-player customization changes /
// periodic sync ticks during gameplay), so this isn't a per-frame hook and
// is cheap. The detour signature matches the Ghidra-decoded prototype:
//     void Replication_UpdateEntityState(uint* param_1)
// Default x64 calling convention (rcx) — single pointer argument.

static constexpr uintptr_t k_hr_replication_rva     = 0x2C8DFC;
static constexpr uintptr_t k_hr_kb_axis_rva         = 0x287FA68;  // int32[81][2]

// Override caches — sentinel INT32_MIN means "leave whatever Replication just
// wrote alone" (we have no override for that tag/slot).
static constexpr int32_t   k_hr_no_override         = INT32_MIN;
static int32_t g_kb_primary_overrides[k_hr_table_tag_count * 2];
static int32_t g_kb_secondary_overrides[k_hr_table_tag_count * 2];
static int32_t g_axis_overrides[k_hr_table_tag_count * 2];
static bool    g_overrides_armed = false;
static HMODULE g_hooked_module   = nullptr;

using ReplicationFn = void (*)(uint32_t* param_1);
static ReplicationFn g_replication_orig = nullptr;

static void apply_binding_overrides_to_tables(uintptr_t base) {
	int32_t* kb_primary   = reinterpret_cast<int32_t*>(base + k_hr_kb_primary_rva);
	int32_t* kb_secondary = reinterpret_cast<int32_t*>(base + k_hr_kb_secondary_rva);
	int32_t* kb_axis      = reinterpret_cast<int32_t*>(base + k_hr_kb_axis_rva);

	for (int i = 0; i < k_hr_table_tag_count * 2; ++i) {
		if (g_kb_primary_overrides[i]   != k_hr_no_override) kb_primary[i]   = g_kb_primary_overrides[i];
		if (g_kb_secondary_overrides[i] != k_hr_no_override) kb_secondary[i] = g_kb_secondary_overrides[i];
		if (g_axis_overrides[i]         != k_hr_no_override) kb_axis[i]      = g_axis_overrides[i];
	}
}

static void __fastcall hr_replication_detour(uint32_t* param_1) {
	// Let Reach's own logic run first — it rewrites the three tables from MCC's
	// raw VK list. Then re-stamp our overrides on top.
	if (g_replication_orig) {
		g_replication_orig(param_1);
	}
	if (g_overrides_armed && g_hooked_module) {
		apply_binding_overrides_to_tables(reinterpret_cast<uintptr_t>(g_hooked_module));
	}
}

// Install the Replication_UpdateEntityState hook. Idempotent — first
// successful install latches via g_hooked_module. Failures log and the writer
// degrades to the legacy "stamp once at module-load" behavior.
static void install_replication_hook(HMODULE mod) {
	if (g_hooked_module == mod) return;        // already hooked this load
	if (g_hooked_module != nullptr) return;    // hooked a previous module — leave it; module rebase across launches not supported here

	static bool s_minhook_initialized = false;
	if (!s_minhook_initialized) {
		MH_STATUS st = MH_Initialize();
		if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			CONSOLE_LOG_WARN("Reach replication hook: MH_Initialize failed (status=%d) — bindings may revert in MP/Forge/Theater", (int)st);
			return;
		}
		s_minhook_initialized = true;
	}

	void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mod) + k_hr_replication_rva);
	void* trampoline = nullptr;
	MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&hr_replication_detour), &trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("Reach replication hook: MH_CreateHook @ %p failed (status=%d) — bindings may revert in MP/Forge/Theater", target, (int)st);
		return;
	}
	g_replication_orig = reinterpret_cast<ReplicationFn>(trampoline);

	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("Reach replication hook: MH_EnableHook @ %p failed (status=%d) — bindings may revert in MP/Forge/Theater", target, (int)st);
		g_replication_orig = nullptr;
		return;
	}

	g_hooked_module = mod;
	CONSOLE_LOG_INFO("Reach replication hook installed @ %p (haloreach+0x%llX)",
		target, (unsigned long long)k_hr_replication_rva);
}

// --- Reach launch state-machine logger -------------------------------------
//
// The four globals below are the externally-observable evidence of how Reach's
// launch state machine progressed. State 4 (ProcessPlaybackFrame) is the
// campaign tick that populates the scenario property; state 5 (FinalizePlayback)
// skips it. State 11 is terminal. If state goes 0→1→2→4→...→11 the campaign
// loaded; if it goes 0→1→2→5→11 PPF was skipped — the smoking gun for our
// "black screen + nvwgf2umx AV" failure mode.

static constexpr uintptr_t k_hr_state_machine_rva     = 0xC1A114;   // real state var
static constexpr uintptr_t k_hr_film_active_rva       = 0x10030A8;  // (DAT_1810030a8 != 0) + 4 → state 5
static constexpr uintptr_t k_hr_psjs_gate_rva         = 0x10030B0;
static constexpr uintptr_t k_hr_ext_launch_gate_rva   = 0x27CD394;

static volatile LONG g_state_logger_started = 0;

static DWORD WINAPI haloreach_state_logger_thread(LPVOID) {
	HMODULE mod = GetModuleHandleW(L"haloreach.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("hr_state_logger: haloreach.dll not loaded — bailing");
		InterlockedExchange(&g_state_logger_started, 0);
		return 0;
	}

	auto base = reinterpret_cast<uintptr_t>(mod);
	auto* state    = reinterpret_cast<volatile uint32_t*>(base + k_hr_state_machine_rva);
	auto* film     = reinterpret_cast<volatile uint32_t*>(base + k_hr_film_active_rva);
	auto* psjs     = reinterpret_cast<volatile uint32_t*>(base + k_hr_psjs_gate_rva);
	auto* ext_gate = reinterpret_cast<volatile uint32_t*>(base + k_hr_ext_launch_gate_rva);

	uint32_t prev_state = 0xFFFFFFFFu;
	uint32_t prev_film  = 0xFFFFFFFFu;
	uint32_t prev_psjs  = 0xFFFFFFFFu;
	uint32_t prev_ext   = 0xFFFFFFFFu;

	// Track every distinct state value seen so we can confirm intermediate
	// transitions (1, 2, 4, etc.) even if our poll cadence doesn't catch
	// them as transitions. Bit i set if state value i was observed.
	uint32_t state_seen_mask = 0;

	const ULONGLONG t0 = GetTickCount64();
	const ULONGLONG kDeadlineMs = 30000;

	CONSOLE_LOG_INFO("hr_state_logger: armed (haloreach base=%p, polling 0.5ms for %ums)",
		(void*)base, (unsigned)kDeadlineMs);

	// SEH-safe peek for each volatile global. The four addresses are
	// computed from haloreach.dll's base + RVA, which is normally valid as
	// soon as the DLL is loaded — but live RE on Reach MP showed at least
	// one of these globals can land in a region that becomes momentarily
	// unreadable during early launch (we crashed at halox+0x2D6E0 with a
	// junk RCX = 0x0FA0... while polling here). This is a diagnostic
	// thread — not load-bearing — so an AV must not propagate; bail
	// gracefully on any bad read.
	auto safe_read_u32 = [](volatile uint32_t* p, uint32_t* out) -> bool {
		__try { *out = *p; return true; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	};

	while (GetTickCount64() - t0 < kDeadlineMs) {
		uint32_t s, f, p, e;
		if (!safe_read_u32(state,    &s) ||
		    !safe_read_u32(film,     &f) ||
		    !safe_read_u32(psjs,     &p) ||
		    !safe_read_u32(ext_gate, &e)) {
			CONSOLE_LOG_WARN("hr_state_logger: bad read on one of state/film/psjs/ext_gate — bailing");
			break;
		}

		if (s < 32) state_seen_mask |= (1u << s);

		if (s != prev_state || f != prev_film || p != prev_psjs || e != prev_ext) {
			ULONGLONG dt = GetTickCount64() - t0;
			CONSOLE_LOG_INFO("hr_state[t=%4ums]: state=%u film=%u psjs=%u ext_gate=%u",
				(unsigned)dt, s, f, p, e);
			prev_state = s;
			prev_film  = f;
			prev_psjs  = p;
			prev_ext   = e;
		}

		// Spin-poll: Sleep(0) yields without going through the timer wheel,
		// so we sample at near-thread-quantum granularity (sub-ms) which is
		// what we need to catch a state machine that completes in < 1 frame.
		// CPU cost is one core for ~30s — acceptable for a one-shot probe.
		SwitchToThread();
	}

	CONSOLE_LOG_INFO("hr_state_logger: states observed during run = 0x%08x (bit i set = state i seen)",
		state_seen_mask);

	CONSOLE_LOG_INFO("hr_state_logger: deadline reached, exiting (final state=%u)", *state);
	InterlockedExchange(&g_state_logger_started, 0);
	return 0;
}

// --- Campaign run_flag override --------------------------------------------

static constexpr uintptr_t k_hr_resolve_and_queue_rva = 0x2A41C4;
static constexpr uintptr_t k_hr_queue_load_rva        = 0x22058;

using HrResolveAndQueueFn = void(*)(void* /*GUID*/, void* /*map_id*/, char /*run_flag*/);
using HrQueueLoadFn       = uint8_t(*)(void* /*GUID*/, void* /*map_id*/);
static HrResolveAndQueueFn  g_hr_resolve_orig    = nullptr;
static HrQueueLoadFn        g_hr_queue_load_orig = nullptr;
static volatile LONG        g_hr_load_force_armed = 0;
static volatile LONG        g_hr_load_hook_installed = 0;
static volatile LONG        g_hr_queue_load_fired   = 0;

static uint8_t hr_queue_load_detour(void* guid, void* map_id) {
	if (InterlockedCompareExchange(&g_hr_queue_load_fired, 1, 0) == 0) {
		CONSOLE_LOG_INFO("hr_queue_load: ENTERED (FUN_180022058 GUID=%p map=%p)", guid, map_id);
	}
	uint8_t r = g_hr_queue_load_orig(guid, map_id);
	if (InterlockedExchange(&g_hr_queue_load_fired, 2) == 1) {
		CONSOLE_LOG_INFO("hr_queue_load: returned %u (first call)", r);
	}
	return r;
}

static void hr_resolve_and_queue_detour(void* guid, void* map_id, char run_flag) {
	char effective = run_flag;
	bool forced = false;
	if (run_flag == 0 && InterlockedCompareExchange(&g_hr_load_force_armed, 0, 1) == 1) {
		effective = 1;
		forced = true;
		CONSOLE_LOG_INFO("hr_resolve_and_queue: forcing run_flag=1 (one-shot, GUID=%p map=%p)", guid, map_id);
	}
	g_hr_resolve_orig(guid, map_id, effective);

	// PPF's call always passes run_flag=0; the inner gates in FUN_1802a41c4
	// (path-string non-empty AND FUN_180398458 returns non-zero AND
	// run_flag != 0) all need to pass for FUN_180022058 (the actual map-load
	// queue) to fire. Empirically the path-resolve table lookup
	// (FUN_18003e2a0 against DAT_18263ec58/ec60) returns empty under our
	// launch path because those registry tables aren't populated. Bypass:
	// call FUN_180022058 directly with the same GUID/map_id once per
	// campaign launch.
	if (forced && g_hr_queue_load_orig) {
		CONSOLE_LOG_INFO("hr_resolve_and_queue: calling FUN_180022058 directly (bypass inner gates)");
		uint8_t r = g_hr_queue_load_orig(guid, map_id);
		CONSOLE_LOG_INFO("hr_resolve_and_queue: FUN_180022058 returned %u", r);
	}
}

void haloreach_install_campaign_load_hook() {
	if (InterlockedCompareExchange(&g_hr_load_hook_installed, 1, 0) != 0) return;

	HMODULE mod = GetModuleHandleW(L"haloreach.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("hr_load_hook: haloreach.dll not loaded");
		InterlockedExchange(&g_hr_load_hook_installed, 0);
		return;
	}

	if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
		CONSOLE_LOG_WARN("hr_load_hook: MH_Initialize failed");
		InterlockedExchange(&g_hr_load_hook_installed, 0);
		return;
	}

	auto base   = reinterpret_cast<uintptr_t>(mod);
	void* target = reinterpret_cast<void*>(base + k_hr_resolve_and_queue_rva);

	if (MH_CreateHook(target,
	                  reinterpret_cast<void*>(&hr_resolve_and_queue_detour),
	                  reinterpret_cast<void**>(&g_hr_resolve_orig)) != MH_OK) {
		CONSOLE_LOG_WARN("hr_load_hook: MH_CreateHook failed at %p", target);
		InterlockedExchange(&g_hr_load_hook_installed, 0);
		return;
	}
	if (MH_EnableHook(target) != MH_OK) {
		CONSOLE_LOG_WARN("hr_load_hook: MH_EnableHook failed at %p", target);
		InterlockedExchange(&g_hr_load_hook_installed, 0);
		return;
	}

	void* qtarget = reinterpret_cast<void*>(base + k_hr_queue_load_rva);
	if (MH_CreateHook(qtarget,
	                  reinterpret_cast<void*>(&hr_queue_load_detour),
	                  reinterpret_cast<void**>(&g_hr_queue_load_orig)) == MH_OK) {
		MH_EnableHook(qtarget);
		CONSOLE_LOG_INFO("hr_queue_load_hook: installed at %p (haloreach+0x%X)",
			qtarget, (unsigned)k_hr_queue_load_rva);
	} else {
		CONSOLE_LOG_WARN("hr_queue_load_hook: MH_CreateHook failed at %p", qtarget);
	}

	CONSOLE_LOG_INFO("hr_load_hook: installed at %p (haloreach+0x%X), armed=%ld",
		target, (unsigned)k_hr_resolve_and_queue_rva, g_hr_load_force_armed);
}

void haloreach_arm_campaign_load_force() {
	InterlockedExchange(&g_hr_load_force_armed, 1);
	CONSOLE_LOG_INFO("hr_load_hook: run_flag override armed (one-shot)");
}

void haloreach_start_state_logger() {
	if (InterlockedCompareExchange(&g_state_logger_started, 1, 0) != 0) return;

	HANDLE h = CreateThread(nullptr, 0, &haloreach_state_logger_thread, nullptr, 0, nullptr);
	if (!h) {
		CONSOLE_LOG_WARN("hr_state_logger: CreateThread failed (gle=%lu)", GetLastError());
		InterlockedExchange(&g_state_logger_started, 0);
		return;
	}
	CloseHandle(h);
}

static HMODULE haloreach_module() {
	auto im = game_instance_manager();
	if (!im) return nullptr;
	if (im->get_game() != _module_haloreach) return nullptr;
	return GetModuleHandleW(L"haloreach.dll");
}

void haloreach_apply_native_overrides() {
	HMODULE mod = haloreach_module();
	if (!mod) return;

	auto* s = mcc_user_settings();
	if (!s->loaded) return;

	uintptr_t base  = reinterpret_cast<uintptr_t>(mod);
	uint8_t*  cache = reinterpret_cast<uint8_t*>(base + k_hr_cache_rva);

	const int want_fov_deg = s->fov[_module_haloreach];        // 0 = no override

	for (int i = 0; i < k_hr_cache_count; ++i) {
		uint8_t* entry = cache + (size_t)i * k_hr_cache_stride;

		// Bit 2 of the flags u32 = "valid" — wait until Reach has populated
		// the slot, otherwise our write gets overwritten by Reach's own init.
		uint32_t flags = *reinterpret_cast<const uint32_t*>(entry + k_hr_off_flags);
		if (!(flags & k_hr_flags_valid_bit)) continue;

		if (want_fov_deg > 0) {
			*reinterpret_cast<float*>(entry + k_hr_off_fov_deg) = (float)want_fov_deg;
		}
	}

	// KB binding overrides — write into the static tag→polled-VK-index tables.
	refresh_polled_vk_list(mod);
	log_polled_vk_dump();

	const auto* kbm_src = s->custom_kbm[_module_haloreach];
	bool any_set = false;
	for (int n = 0; n < k_game_abstract_button_count; ++n) {
		if (kbm_src[n].abstract_button >= 0) { any_set = true; break; }
	}
	if (!any_set) return;

	int32_t* kb_primary = reinterpret_cast<int32_t*>(base + k_hr_kb_primary_rva);

	// Per-launch summary of what we wrote and what we skipped, so the user
	// can see exactly which bindings made it through.
	if (!g_logged_writes) {
		g_logged_writes = true;
		static const char* libmcc_names[k_game_abstract_button_count] = {
			"jump","switchgrenade","actionreload","reload","switchweapon",
			"meleeattack","flashlight","throwgrenade","fire","crouch",
			"zoom","zoomin","zoomout","swapweapon","sprint","bansheebomb",
			"moveforward","movebackward","strafeleft","straferight",
			"showscores","primaryvehicletrick","secondaryvehicletrick","equipment",
			"secondaryfire","lifteditor","dropeditor","grabobjecteditor",
			"boosteditor","croucheditor","deleteobjecteditor","createobjecteditor",
			"opentoolmenueditor","switchplayermodeeditor","scopezoomeditor",
			"playerlockformanipulationeditor","showhidepanneltheater",
			"showhideinterfacetheater","togglefirstthirdpersonviewtheater",
			"camerafocustheater","fastforwardtheater","fastrewindtheater",
			"stopcontinueplaybacktheater","playbackspeeduptheater",
			"enterfreecameramodetheater","movementspeeduptheater",
			"panningcameratheater","cameramoveuptheater","cameramovedowntheater",
			"dualwield","zoomcameratheater","togglerotationaxeseditor",
			"duplicateobjecteditor","lockobjecteditor","resetorientationeditor",
			"reloadsecondary","previousgrenade","specialaction","loadoutmenu",
			"activatewaypoint","activatewaypointalt","pingnavpoints",
			"raisehornet","lowerhornet","flashlightalt","nextgrenade",
		};
		for (int n = 0; n < k_game_abstract_button_count; ++n) {
			if (kbm_src[n].abstract_button < 0) continue;
			int8_t tag = k_libmcc_to_haloreach_tag[n];
			uint16_t vk0 = (uint16_t)kbm_src[n].virtual_key_codes[0];
			if (tag < 0) {
				CONSOLE_LOG_WARN("Reach bind: %-22s VK=0x%02X tag=UNMAPPED",
					libmcc_names[n], vk0);
				continue;
			}
			int idx0 = vk_to_polled_index(vk0);
			if (idx0 < 0) {
				CONSOLE_LOG_WARN("Reach bind: %-22s VK=0x%02X tag=0x%02X skipped (VK not in polled list)",
					libmcc_names[n], vk0, tag);
			} else {
				CONSOLE_LOG_INFO("Reach bind: %-22s VK=0x%02X tag=0x%02X → polled idx %d",
					libmcc_names[n], vk0, tag, idx0);
			}
		}
	}

	// Reset override caches each call so a re-apply (e.g. settings reload)
	// doesn't carry over a stale binding.
	for (int i = 0; i < k_hr_table_tag_count * 2; ++i) {
		g_kb_primary_overrides[i]   = k_hr_no_override;
		g_kb_secondary_overrides[i] = k_hr_no_override;
		g_axis_overrides[i]         = k_hr_no_override;
	}

	for (int n = 0; n < k_game_abstract_button_count; ++n) {
		int8_t tag = k_libmcc_to_haloreach_tag[n];
		if (tag < 0 || tag >= k_hr_table_tag_count) continue;
		if (kbm_src[n].abstract_button < 0) continue;

		uint16_t vk0 = (uint16_t)kbm_src[n].virtual_key_codes[0];
		int idx0 = vk_to_polled_index(vk0);
		if (idx0 >= 0) {
			kb_primary[tag * 2 + 0] = idx0;
			// Mirror into the override cache so the post-Replication re-stamp
			// puts the same value back if Reach overwrites it.
			g_kb_primary_overrides[tag * 2 + 0] = idx0;
		}

		uint16_t vk1 = (uint16_t)kbm_src[n].virtual_key_codes[1];
		if (vk1 != 0) {
			int idx1 = vk_to_polled_index(vk1);
			if (idx1 >= 0) {
				kb_primary[tag * 2 + 1] = idx1;
				g_kb_primary_overrides[tag * 2 + 1] = idx1;
			}
		}
	}

	g_overrides_armed = true;

	// Install the Replication_UpdateEntityState detour so MP/Forge/Theater
	// customization syncs don't clobber our writes. Only attempted once per
	// haloreach.dll load — `install_replication_hook` is idempotent.
	install_replication_hook(mod);
}

