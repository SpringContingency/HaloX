#include "halo2_native_overrides.h"

#include "game_instance_manager.h"
#include "mcc_user_settings.h"
#include "../input/win32_input.h"
#include "../logging/logging.h"
#include "../main/main.h"  // for g_show_imgui

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

#include <MinHook.h>

using namespace libmcc;

// --- halo2.dll input architecture (RE'd) -----------------------------------
//
// Halo 2 PC's input is VK-direct — there is no configurable binding table in
// the DLL. The function `FUN_1806D1620(short vk)` returns a held-count for
// the given Win32 VK (also handles a few synthetic axis tags 0x101..0x104).
// Every gameplay query for an action hard-codes the default VK at the call:
//
//     FUN_1806D5FA0  (player input → engine command)  hard-codes
//        W=0x57 S=0x53 A=0x41 D=0x44 R=0x52 F=0x46 T=0x54 G=0x47
//     FUN_1806C4CE0  hard-codes  F1=0x70  ("switch local player")
//     FUN_18073B200  hard-codes  SPACE=0x20  ENTER=0x0D  ('A'=0x41)  (pause)
//
// The 256-entry "key state" arrays sit at:
//     DAT_1815EA68C  // u8[256]   — held-tick counter
//     DAT_1815EA78C  // u16[256]  — held duration
//     DAT_1815EA98C  // u8[256]   — debounce / "is-down-now"
// They are populated each frame by FUN_1806D28F0, which loops VKs 0..0xFF and
// calls GetAsyncKeyState(vk) per-slot.
//
// Strategy: hook FUN_1806D1620 and substitute the user-remapped VK on the
// way in. When the user (in MCC settings) maps "fire" → V, we record
// remap[VK_LBUTTON] = 'V'. The first time the game polls FUN_1806D1620(1)
// to ask "is LMB held?", we detour to FUN_1806D1620_orig('V') instead.
//
// This is the only sane override path: there is nothing else to write to.
// There are zero data writers to the binding-related globals (because there
// are no bindings — only state arrays).

static constexpr uintptr_t k_h2_is_key_held_rva     = 0x6D1620;
// FUN_1806D16F0(short vk) — reads `DAT_1815EA78C` (the u16 dur array).
// Single caller is FUN_1806D8AB0 (the per-player binding-table dispatcher,
// the one that walks each binding slot and decides if an action is "active").
// type-1 (keyboard / mouse) bindings flow through here for fire / zoom /
// melee / crouch / jump / reload / etc. Hooking this means ALL binding-
// dispatched actions see halox's win32_input state directly — bypassing
// halo2's own GetAsyncKeyState poller, which runs on the game thread and
// can fail to see WM_LBUTTONDOWN / WM_KEYDOWN that halox's wndproc has
// already consumed.
static constexpr uintptr_t k_h2_dur_read_rva        = 0x6D16F0;
// Live instrumentation: FUN_1806D9AF0(player, action_id) returns the per-frame
// "is action active" value (DAT_1815F1E30[player*0xc0 + action_id]). Hooking
// it on rising edge gives one log line per key-press per action_id, which is
// what we need to ground-truth the action_index ↔ libmcc-button map by play-
// testing instead of guessing from the FUN_1806D9D70 decompile.
static constexpr uintptr_t k_h2_action_lookup_rva   = 0x6D9AF0;

// Per-frame poller — calls GetAsyncKeyState for VKs 0..0xFF and fills the
// state arrays below. We hook this AFTER the original runs to mirror
// state[default_vk] = state[user_vk] for any remapped binding, which catches
// inlined / direct-array readers that bypass FUN_1806D1620.
static constexpr uintptr_t k_h2_poller_rva       = 0x6D28F0;
// Halo2's preferred image base is 0x180000000.
static constexpr uintptr_t k_h2_state_held_rva   = 0x15EA68C;  // u8[256]   held-tick counter
static constexpr uintptr_t k_h2_state_dur_rva    = 0x15EA78C;  // u16[256]  held duration
static constexpr uintptr_t k_h2_state_down_rva   = 0x15EA98C;  // u8[256]   debounce / "is-down-now"
// Per-player per-action active-state byte array. Indexed [player * 0xC0 + action_id].
// Read by FUN_1806D9AF0; written by FUN_1806D8AB0 (binding dispatcher) each frame.
// Polling this directly each frame gives us the empirical action_id ↔ key map:
// a 0→1 transition on byte N when the user pressed key K means action N fires on K.
static constexpr uintptr_t k_h2_action_state_rva = 0x15F1E30;

// --- Binding table -----------------------------------------------------------
// Per-player binding table at:
//     0x1815EB7A0 + 0x17C4 * player + 0x1C
// Each player has 60 actions @ stride 0x64 (25 ints / 100 bytes).
// Per action layout (25 ints): [count_word, padding_word, slot0, slot1, ... slot7]
// where each slot is 3 ints (12 bytes): [type, key_id, threshold].
//   type 0 = mouse button / mouse axis (key_id 0=LMB,1=RMB,2=MMB; 0xC/0xD=wheel)
//   type 1 = keyboard VK (key_id is a Win32 VK)
//   type 2 = gamepad axis / button
//
// Action layout in the player's table:
//   action[i] starts at +0x1C + i*0x64 from player base
// At the int* level (param_1 in FUN_1806D9D70 decompile), the action header
// counter for action_index N lives at index 7 + 25*N.
//
// In FUN_1806D9D70 the `bVar5` flag selects the KB/M branch; we extracted the
// (action_index → default_vk) mapping for KB/M from that decompile.

static constexpr uintptr_t k_h2_binding_table_rva = 0x15EB7A0;       // base of per-player binding table (VA)
static constexpr uintptr_t k_h2_player_stride     = 0x17C4;           // bytes per player slot
static constexpr uintptr_t k_h2_action_table_off  = 0x1C;             // first action header within a player slot
static constexpr uintptr_t k_h2_action_stride     = 0x64;             // bytes per action (25 ints)
static constexpr int       k_h2_max_slots_per_action = 8;              // 8 binding slots per action

// Field offsets within each binding slot (3 ints @ +0xC). The action header is
// 8 bytes (count word + padding); slot 0 starts at +8 within the action entry.
static constexpr int       k_h2_slot_field_type      = 0;             // int32 type
static constexpr int       k_h2_slot_field_key_id    = 1;             // int32 key_id
static constexpr int       k_h2_slot_field_threshold = 2;             // int32 threshold

// Halo 2's hardcoded default VKs for the abstract actions we want to remap.
// Anything in this list is something halo2 polls via FUN_1806D1620(vk) at
// least once in gameplay code. If user has not remapped, default VK passes
// through untouched.
//
// Sources:
//   - FUN_1806D5FA0 (player input → command struct):
//        W=0x57 S=0x53 A=0x41 D=0x44 R=0x52 F=0x46 T=0x54 G=0x47
//   - Standard halo2 default keybinds (per the published controls.cfg):
//        Fire=LBUTTON(1), Melee=Q(0x51), Crouch=LCONTROL(0xA2),
//        Jump=SPACE(0x20), Grenade=RBUTTON(2), SwitchWeapon=2 (number key)
//        Flashlight=F(0x46), Zoom=MBUTTON(4)/E(0x45)
//        Reload=R(0x52), Action=E(0x45)
//
// LIKELY confidence on most of these — fire/melee/crouch/jump are documented
// halo2 PC defaults; movement keys are CONFIRMED by the decompile.

struct s_halo2_default_bind {
	int  abstract_button;   // libmcc::e_game_abstract_button index
	uint8_t default_vk;     // halo2's hardcoded VK for that action
};

static const s_halo2_default_bind k_halo2_defaults[] = {
	// CONFIRMED via FUN_1806D5FA0 decompile (direct-VK bypass path).
	//
	// Why we don't add fire / grenade / melee / crouch / zoom / jump /
	// actionreload / switchweapon / showscores: deeper RE (see memory note
	// project_halo2_profile_bypass) revealed halo2 actually HAS a per-player
	// binding table at 0x1815EB7A0+0x17C4*player+0x1C dispatched by
	// FUN_1806D8AB0. Each binding has a TYPE field:
	//   - Type 0: mouse button / mouse axis (read directly from a control
	//             struct — no VK is ever polled). fire/secondary-fire/grenade
	//             default to type-0 in the KB/M branch of FUN_1806D9D70, so
	//             VK→VK substitution can NEVER remap them via the current
	//             FUN_1806D1620 detour or state-array mirror.
	//   - Type 1: keyboard VK (read via FUN_1806D16F0 from DAT_1815EA78C).
	//             Our poller hook's mirror_state_arrays() copies dur[user_vk]
	//             into dur[default_vk], which DOES catch type-1 bindings — but
	//             only if the (action_index → libmcc_button) map is correct.
	//             FUN_1806D9D70's KB/M branch writes SPACE to actions 0, 1, 2,
	//             AND 19, so picking the wrong index cross-fires. Until each
	//             action_index is play-tested or call-tree-confirmed, leave
	//             these out — the previous "LIKELY" guesses caused remap
	//             chaos (zoom→melee+grenade, throw_grenade→crouch).
	//   - Type 2: gamepad axis — irrelevant for KB/M.
	//
	// The six entries below all flow through FUN_1806D5FA0's hardcoded
	// VK-poll bypass (not the binding table), which is why the simple
	// FUN_1806D1620 detour + state-array mirror works for them.
	{ _game_abstract_button_moveforward,   0x57 }, // W   — CONFIRMED via FUN_1806D5FA0 @ 1806d62c5
	{ _game_abstract_button_movebackward,  0x53 }, // S   — CONFIRMED via FUN_1806D5FA0 @ 1806d6305
	{ _game_abstract_button_strafeleft,    0x41 }, // A   — CONFIRMED via FUN_1806D5FA0 @ 1806d6335
	{ _game_abstract_button_straferight,   0x44 }, // D   — CONFIRMED via FUN_1806D5FA0 @ 1806d6347
	{ _game_abstract_button_reload,        0x52 }, // R   — CONFIRMED via FUN_1806D5FA0 @ 1806d6359
	{ _game_abstract_button_flashlight,    0x46 }, // F   — CONFIRMED via FUN_1806D5FA0 @ 1806d637d
	// Throwgrenade — CONFIRMED via FUN_1806D5FA0 hardcoded G poll. Engine ALSO
	// has a separate switchgrenade action_id (default G) in the binding table,
	// so without this remap pressing G triggers throw via FUN_1806D5FA0 AND
	// switch via the binding-table dispatcher. Adding throwgrenade here makes
	// is_key_held(G) read the user's chosen throw key — so FUN_1806D5FA0's G
	// poll only fires throw when the user presses their throw key, leaving
	// G-press free to fire switchgrenade alone.
	{ _game_abstract_button_throwgrenade,  0x47 }, // G
};

// 256-entry VK→VK substitution table. Initialized to identity (default[i] = i).
// On apply, for each abstract action with a user override, we set
// remap[default_vk] = user_vk. The detour reads remap[incoming_vk] and forwards
// the substituted VK to the trampoline.
static uint16_t  g_vk_remap[256];
static bool      g_remap_armed = false;

// --- action_index → libmcc-button map (derived from FUN_1806D9D70 decompile) -
//
// The KB/M branch of the per-player default-bindings initializer writes the
// (type, key_id) pair for each action_index. Below we map each abstract libmcc
// button to the action_index slot(s) it corresponds to in halo2's binding table.
//
// Sources / confidence:
//   CONFIRMED — matched halo2's KB/M-branch (bVar5=true) write of the documented
//               PC default for that abstract action.
//   LIKELY    — single internal slot whose KB/M default key matches a known
//               halo2 PC default but the libmcc-button↔slot tie isn't 100%.
//   FALLBACK  — also written by the same default VK in another slot; included
//               so that remapping the libmcc button covers all readers.
//
// Multiple action_indices can share a libmcc abstract button (e.g. SPACE is
// written to actions 0, 1, 2 — likely on-foot jump / vehicle jump / airborne
// jump). We write all of them on remap so every reader sees the new VK.

struct s_h2_action_entry {
	int abstract_button;          // libmcc::e_game_abstract_button
	int action_indices[6];        // -1 terminator
	uint8_t default_vk;           // KB/M-branch default key_id (type-1) or 0 if type-0
	uint8_t default_type;         // 0 mouse / 1 keyboard
};

// Each row: { abstract, {action_indices...,-1}, default_vk_or_keyid, default_type }
//
// CORRECTED via live binding-table dump from MCC halo2 (memory @ halo2.dll+0x15EB7BC,
// 60 actions × 100 bytes each). The earlier guesses from the FUN_1806D9D70 decompile
// were systematically wrong because the reader was misinterpreting the action layout.
// Live dump — for each action_idx N, slot 0 = (kb_or_mouse, key_id):
//
//   0=SPACE   1=SPACE   2=LCTRL   3=G        4=TAB     5=MMB+Q   6=Z       7=Q
//   8=RMB     9=RMB    10=LMB    11=PAUSE   12=H     13=RMB    14=LSHIFT 15-18=axes
//  19=E      20=TAB    21=padaxis 22=C+pad  23=LMB   24=LMB    25=RMB    26=VK0x103
//  27=VK0x103 28=mouse_axisX 29=axisY 30=VK0x103 31=R+pad 32=P+pad 33=H 34-41=E
//  42-43=X   44-45=TAB  46=R     47=E      48=W      49=S      50=A      51=D
//
// Type for slot 0 in the live data is uniformly 1 (keyboard). The earlier "type-0
// mouse default for fire/grenade" finding from the static decompile didn't match
// reality — fire (action 10) is bound as type-1 with key_id = VK_LBUTTON in the
// live table. (GetKeyState handles VK_LBUTTON / VK_RBUTTON / VK_MBUTTON identically
// to keyboard keys, so the type-1 path works for all of them.)
static const s_h2_action_entry k_h2_action_table[] = {
	// Movement — slot 0 type=1 in dump
	{ _game_abstract_button_moveforward,    { 48, -1, -1, -1, -1, -1 }, 0x57, 1 },
	{ _game_abstract_button_movebackward,   { 49, -1, -1, -1, -1, -1 }, 0x53, 1 },
	{ _game_abstract_button_strafeleft,     { 50, -1, -1, -1, -1, -1 }, 0x41, 1 },
	{ _game_abstract_button_straferight,    { 51, -1, -1, -1, -1, -1 }, 0x44, 1 },

	// Jump — actions 0, 1 both bound to SPACE.
	{ _game_abstract_button_jump,           {  0,  1, -1, -1, -1, -1 }, 0x20, 1 },

	// Crouch — action 2 = LCTRL.
	{ _game_abstract_button_crouch,         {  2, -1, -1, -1, -1, -1 }, 0xA2, 1 },

	// Throw grenade — action 3 = G.
	{ _game_abstract_button_throwgrenade,   {  3, -1, -1, -1, -1, -1 }, 0x47, 1 },

	// Switch weapon — action 4 fires when the user's swap-weapon key is pressed.
	// Empirical: live dump shows action 4 default = TAB (key 0x09); user's MCC
	// binding sets it (often back to TAB or to a dedicated key). Earlier code
	// had showscores → 4 which clobbered swap-weapon when scoreboard wasn't
	// remapped — corrected.
	{ _game_abstract_button_switchweapon,   {  4, -1, -1, -1, -1, -1 }, 0x09, 1 },

	// Show scoreboard — actions 20, 44, 45 are TAB in their respective contexts
	// (action 4 is swap-weapons, despite also being TAB by default).
	{ _game_abstract_button_showscores,     { 20, 44, 45, -1, -1, -1 }, 0x09, 1 },

	// Melee — action 5 has Q AND MMB as slot 0/1; user remaps the slot we own (slot 0).
	{ _game_abstract_button_meleeattack,    {  5, -1, -1, -1, -1, -1 }, 0x04, 1 },

	// Reload / Flashlight / Reloadsecondary — DELIBERATELY NOT WRITTEN to the
	// binding table. We tried both (reload→6, flashlight→31) and the swap
	// (reload→31, flashlight→6); the user reports both reload and flashlight
	// firing on Z either way. Conclusion: action_ids 6 / 31 / 46 don't map
	// cleanly 1:1 to libmcc reload / flashlight / reloadsecondary semantics —
	// likely each abstract button drives multiple engine actions (primary +
	// dual-wield + vehicle contexts) and the relationship isn't 1:1.
	//
	// Instead, rely on FUN_1806D5FA0's direct-VK polling path, which polls
	// VK_R for reload and VK_F for flashlight unconditionally. With the user's
	// remap encoded into g_vk_remap (k_halo2_defaults handles those two), the
	// FUN_1806D1620 detour returns the correct held state for each press.
	// Engine action_ids 6 (default Z) and 31 (default R) keep their factory
	// defaults — pressing the engine's default key still fires the engine's
	// default action_id. If the user picks a non-default key for reload or
	// flashlight, the binding-table dispatcher (FUN_1806D8AB0) will not see
	// it, but the direct-VK path will — which is the path that actually
	// drives the gameplay command struct in halo2.
	//
	// TODO(re-RE): query Ghidra to identify which engine action_id is
	// definitively reload vs flashlight (probe FUN_1806D8AB0's output bytes
	// in DAT_1815F1E30[player*0xC0 + action_id] and correlate with observed
	// gameplay), then re-add only the verified mapping.

	// Fire — actions 10, 23, 24 all bound to LMB (10 primary; 23/24 are dual-wield contexts).
	{ _game_abstract_button_fire,           { 10, 23, 24, -1, -1, -1 }, 0x01, 1 },

	// Zoom — actions 8, 9, 25 all bound to RMB.
	{ _game_abstract_button_zoom,           {  8,  9, 25, -1, -1, -1 }, 0x02, 1 },

	// Action / use ("E to pick up") — actions 19, 34..41, 47 all bound to E.
	{ _game_abstract_button_actionreload,   { 19, 34, 35, 36, 37, -1 }, 0x45, 1 },
};

// Resolved per-player binding table base (set after first successful resolve).
// For player 0 only — multi-player launcher isn't a target.
static uint8_t* g_h2_binding_player0 = nullptr;

// Track which (action_index, slot) pairs we've already overwritten so each
// frame's apply call is idempotent (we only patch what we own; we don't keep
// re-stamping the table entry once it matches the user's choice).
//
// Layout: g_owned_slot[action_index] is the slot index we own (0..7), or -1 if
// we haven't written this action yet. We always own slot 0 for any action we
// modify (we collapse the multi-slot list down to "slot 0 = our user binding").
static int8_t  g_h2_owned_slot[60];
static bool    g_h2_owned_init = false;

// Write a single (type, key_id) tuple into the binding table for `action_idx`,
// preserving the existing threshold byte. Returns true on success.
//
// Layout reminder (from FUN_1806D9D70 decompile, indexed at int32 stride):
//   header at [7 + 25*action_idx]  = count
//   slot s in [11 + 25*action_idx + 3*s ... +2]:  [type, key_id, threshold]
// Slot 0 starts at int32 index (8 + 25*action_idx + 3*0) — but the decompile
// shows the first slot's type at +(header+1) where header = 7 + 25*idx. That
// would put slot 0 at +(8 + 25*idx). Verified: (uVar27 * 3 + (header+1)) when
// uVar27 == 0 (the first slot) gives type = param_1[header + 1] which is at
// int32 index (8 + 25*idx). So slot 0 = byte offset 0x20 + 0x64*idx within the
// player table (matches +0x1C + 4 = +0x20).
static bool h2_write_binding_slot(uint8_t* player_base, int action_idx, int slot,
                                   int type, int key_id) {
	if (!player_base) return false;
	if (action_idx < 0 || action_idx >= 60) return false;
	if (slot < 0 || slot >= k_h2_max_slots_per_action) return false;

	// Action entry layout — VERIFIED via live binding-table dump from MCC halo2:
	//   +0..3   count (int32)
	//   +4..7   slot 0 type
	//   +8..11  slot 0 key_id
	//   +12..15 slot 0 threshold
	//   +16..27 slot 1 (type, key_id, threshold)
	//   ...
	// Earlier code used +8 for slot 0 type which was clobbering key_id with type
	// and triggering the wrong actions on remap.
	uintptr_t action_off = k_h2_action_table_off + (uintptr_t)action_idx * k_h2_action_stride;
	uintptr_t slot_off   = action_off + 4 + (uintptr_t)slot * 12;

	int32_t* p = reinterpret_cast<int32_t*>(player_base + slot_off);
	int32_t old_threshold = p[k_h2_slot_field_threshold];
	p[k_h2_slot_field_type]      = type;
	p[k_h2_slot_field_key_id]    = key_id;
	// Preserve threshold (don't clobber the per-action default the engine wrote).
	p[k_h2_slot_field_threshold] = old_threshold;
	return true;
}

// Read the count word for a given action.
static int32_t h2_read_action_count(uint8_t* player_base, int action_idx) {
	if (!player_base) return 0;
	if (action_idx < 0 || action_idx >= 60) return 0;
	uintptr_t action_off = k_h2_action_table_off + (uintptr_t)action_idx * k_h2_action_stride;
	int32_t* p = reinterpret_cast<int32_t*>(player_base + action_off);
	return *p;
}

// Resolve player 0's binding-table base from a freshly attached halo2 module.
// This is just `module_base + 0x15EB7A0 + 0x17C4 * 0`.
static uint8_t* h2_resolve_binding_table_player0(HMODULE mod) {
	if (!mod) return nullptr;
	uintptr_t base = reinterpret_cast<uintptr_t>(mod);
	return reinterpret_cast<uint8_t*>(base + k_h2_binding_table_rva /* + 0x17C4 * 0 */);
}

// Forward declaration; defined below near the FUN_1806D1620 detour.
static uint16_t normalize_vk(uint16_t vk);

// Apply user remappings to the per-player binding table. Idempotent: writes
// only what's actually different from the engine's current state. Called every
// frame from halo2_apply_native_overrides() — the binding table itself is only
// initialized on player-init (FUN_1806D9D70), so once we've patched it our
// writes survive until the next init. We re-apply unconditionally as cheap
// insurance against any code path we don't know about that might re-init.
static void h2_apply_binding_table_overrides() {
	uint8_t* player0 = g_h2_binding_player0;
	if (!player0) return;

	auto* s = mcc_user_settings();
	if (!s || !s->loaded) return;

	const auto* kbm = s->custom_kbm[_module_halo2];

	for (size_t k = 0; k < sizeof(k_h2_action_table) / sizeof(k_h2_action_table[0]); ++k) {
		const s_h2_action_entry& row = k_h2_action_table[k];
		const auto& entry = kbm[row.abstract_button];
		if (entry.abstract_button < 0) continue;             // unset → leave default
		uint16_t user_vk = (uint16_t)entry.virtual_key_codes[0];
		if (user_vk == 0) continue;                           // sentinel for "no override"
		user_vk = normalize_vk(user_vk);
		if (user_vk >= 256) continue;

		// Note: don't try to skip "no-op" writes by comparing user_vk to
		// row.default_vk. The row's default_vk is what we *believe* the engine
		// puts in slot 0, but several actions diverge — e.g. reload's row
		// default_vk = R (the halo2 PC documented default) but the live binding
		// table has Z in slot 0 of action 6. If user picks R and we skip, slot 0
		// stays Z and pressing R does nothing. Always write — it's idempotent
		// on the table and cheap.

		// Walk every action_index this libmcc button covers and stamp slot 0
		// with type=1, key_id=user_vk. Preserves threshold. Type-0 actions are
		// converted to type-1 (mouse default is lost, but it's now remappable).
		for (int i = 0; i < (int)(sizeof(row.action_indices) / sizeof(row.action_indices[0])); ++i) {
			int action_idx = row.action_indices[i];
			if (action_idx < 0) break;

			// Stamp slot 0 with the user's chosen keyboard key.
			h2_write_binding_slot(player0, action_idx, /*slot=*/ 0,
			                      /*type=*/ 1, /*key_id=*/ (int)user_vk);

			// Clear OTHER type=1 (keyboard) slots whose key_id DIFFERS from the
			// user's chosen key. Two-key co-fire problem: action 6 (default Z,
			// count=3) would still fire on Z after we wrote the user's reload
			// key to slot 0, because slots 1+ kept Z. Solution: zero slot 1+
			// when their key_id != user_vk.
			//
			// CRITICAL: do NOT clear slots whose key_id MATCHES user_vk. Halo 2
			// uses the multi-slot pattern with the SAME key + different
			// threshold to distinguish tap vs hold (e.g. action 19/47 have
			// slot 0 = E threshold=tap and slot 1 = E threshold=hold for "tap E
			// to swap weapon" vs "hold E to pick up"). Zeroing the matching
			// slot 1 was the cause of hold-E for pickup not registering.
			// Type=0 (mouse) and type=2 (gamepad) slots are preserved so
			// controller bindings keep working.
			int32_t count = h2_read_action_count(player0, action_idx);
			if (count > 1 && count <= k_h2_max_slots_per_action) {
				uintptr_t action_off = k_h2_action_table_off + (uintptr_t)action_idx * k_h2_action_stride;
				for (int s = 1; s < count; ++s) {
					uintptr_t slot_off = action_off + 4 + (uintptr_t)s * 12;
					int32_t* p = reinterpret_cast<int32_t*>(player0 + slot_off);
					if (p[0] == 1 /*type=keyboard*/ && p[1] != (int32_t)user_vk) {
						p[1] = 0;  // key_id=0 → reads dur[0] which is always 0 → never fires.
					}
				}
			}
		}
	}
}

// MCC's ini stores side-specific VKs (LCTRL=0xA2, RCTRL=0xA3). Halo 2 only
// polls combined-mod VKs in some places — normalize to combined for safety.
static uint16_t normalize_vk(uint16_t vk) {
	switch (vk) {
	case 0xA0: case 0xA1: return 0x10;  // LSHIFT/RSHIFT  → SHIFT
	case 0xA2: case 0xA3: return 0x11;  // LCTRL/RCTRL   → CTRL
	case 0xA4: case 0xA5: return 0x12;  // LALT/RALT     → MENU
	default:              return vk;
	}
}

// Detour fn-ptr type — Ghidra signature: `ulonglong FUN_1806D1620(short vk)`
// __fastcall: vk in CX (16-bit zero-extended to RCX).
using IsKeyHeldFn = unsigned long long (*)(short vk);

// Same signature for FUN_1806D16F0 (the dur-array reader called by the
// binding-table dispatcher). Returns a u16 held duration in the low bits.
using DurReadFn = unsigned long long (*)(short vk);

// Poller: signature unknown; declare 4-arg pass-through so any args halo2
// passes in rcx/rdx/r8/r9 are forwarded to the trampoline unchanged.
using PollFn = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

// Action-state lookup: declare 4-arg pass-through (Ghidra signature is 2-arg
// {player, action_id}, extras forwarded harmlessly).
using ActionLookupFn = unsigned long long (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);

static IsKeyHeldFn    g_is_key_held_orig    = nullptr;
static DurReadFn      g_dur_read_orig       = nullptr;
static PollFn         g_poll_orig           = nullptr;
static ActionLookupFn g_action_lookup_orig  = nullptr;
static HMODULE        g_hooked_module       = nullptr;
static bool           g_minhook_initialized = false;
static uint8_t        g_h2_action_last_active[256] = {};

// Mirror state arrays so direct-readers see remapped state too.
// Walks g_vk_remap: for each (default_vk, user_vk) pair where user_vk != default,
// copies state[user_vk] → state[default_vk] across all three arrays. Game code
// querying state[default_vk] directly (inlined / bypassing FUN_1806D1620) now
// sees the value of the user's actual key.
static void mirror_state_arrays() {
	if (!g_hooked_module) return;
	auto base = reinterpret_cast<uintptr_t>(g_hooked_module);
	auto* held = reinterpret_cast<uint8_t*> (base + k_h2_state_held_rva);
	auto* dur  = reinterpret_cast<uint16_t*>(base + k_h2_state_dur_rva);
	auto* down = reinterpret_cast<uint8_t*> (base + k_h2_state_down_rva);
	for (int default_vk = 0; default_vk < 256; ++default_vk) {
		uint16_t user_vk = g_vk_remap[default_vk];
		if (user_vk == default_vk || user_vk >= 256) continue;
		held[default_vk] = held[user_vk];
		dur [default_vk] = dur [user_vk];
		down[default_vk] = down[user_vk];
	}
}

// Force-stamp halo2's state arrays from win32_input's tracked KB+mouse state.
// Halo2's poller (FUN_1806D28F0) has two branches gated by FUN_180038600:
//   - GetAsyncKeyState branch (when DAT_181E8CE68 == 0) — should poll mouse VKs
//     fine, but raw-input + ImGui capture in halox can interfere.
//   - Gate-array branch (when DAT_181E8CE68 != 0) — does NOT call
//     GetAsyncKeyState; it only increments dur for VKs whose entry in
//     DAT_1815EA194 is non-zero, fed by an external feeder we don't control.
// Either way, by the time halo2's dispatcher reads dur[vk], we want our
// win32_input-tracked state to be authoritative. So after the engine's poll
// runs we overwrite the dur/held/down arrays from g_keyboard + g_mouse_buttons.
//
// Tick semantics: we increment dur[vk] each frame the key is held (saturating
// at 0xFFFF), and reset to 0 the frame it's released. held[vk] is a u8 frame
// counter same as dur but capped to 0xFF. down[vk] is a debounce/pulse flag —
// we set it to 1 on the rising-edge frame and back to 0 next frame so the
// "press once" readers see a single-frame pulse.
static void stamp_state_from_win32_input() {
	if (!g_hooked_module) return;
	auto base = reinterpret_cast<uintptr_t>(g_hooked_module);
	auto* held = reinterpret_cast<uint8_t*> (base + k_h2_state_held_rva);
	auto* dur  = reinterpret_cast<uint16_t*>(base + k_h2_state_dur_rva);
	auto* down = reinterpret_cast<uint8_t*> (base + k_h2_state_down_rva);

	uint8_t kb[256] = {};
	int btn = win32_input_peek_held(kb);

	// Map mouse-button bits to VKs.
	bool mouse_down[5] = {
		(btn & 0x01) != 0,   // LMB → VK 0x01
		(btn & 0x02) != 0,   // RMB → VK 0x02
		(btn & 0x04) != 0,   // MMB → VK 0x04
		(btn & 0x08) != 0,   // X1  → VK 0x05
		(btn & 0x10) != 0,   // X2  → VK 0x06
	};

	// Assemble final "is held" array indexed by VK 0..255. Mouse VKs override.
	uint8_t is_held[256];
	for (int vk = 0; vk < 256; ++vk) is_held[vk] = kb[vk];
	is_held[0x01] = mouse_down[0] ? 1 : 0;
	is_held[0x02] = mouse_down[1] ? 1 : 0;
	is_held[0x04] = mouse_down[2] ? 1 : 0;
	is_held[0x05] = mouse_down[3] ? 1 : 0;
	is_held[0x06] = mouse_down[4] ? 1 : 0;

	// Combined-modifier VKs (0x10/0x11/0x12) are "either side held" — fold the
	// side-specific WM_KEYDOWN-tracked LSHIFT/RSHIFT/etc. up into the combined
	// VK so binding-table writers using either form see the press.
	if (kb[0xA0] || kb[0xA1]) is_held[0x10] = 1;
	if (kb[0xA2] || kb[0xA3]) is_held[0x11] = 1;
	if (kb[0xA4] || kb[0xA5]) is_held[0x12] = 1;
	// And vice versa: if combined-VK is somehow set but no side-specific is,
	// project it onto LSHIFT (the one halo2's defaults reference).
	if (is_held[0x10] && !kb[0xA0] && !kb[0xA1]) is_held[0xA0] = 1;
	if (is_held[0x11] && !kb[0xA2] && !kb[0xA3]) is_held[0xA2] = 1;
	if (is_held[0x12] && !kb[0xA4] && !kb[0xA5]) is_held[0xA4] = 1;

	for (int vk = 0; vk < 256; ++vk) {
		if (is_held[vk]) {
			// Saturating increment.
			if (held[vk] < 0xFF) held[vk] = (uint8_t)(held[vk] + 1);
			uint32_t d = (uint32_t)dur[vk] + 1;
			if (d > 0xFFFF) d = 0xFFFF;
			dur[vk] = (uint16_t)d;
			// Pulse: set down = 1 only on the first frame a key transitions
			// from "not held" (held was 0 before the increment above).
			down[vk] = (held[vk] == 1) ? 1 : 0;
		} else {
			held[vk] = 0;
			dur[vk] = 0;
			down[vk] = 0;
		}
	}
}

// Per-VK held-frame counter, updated each call. Returning a monotonically
// rising value while held lets us beat any threshold the binding table sets
// (the largest seen in live dumps is 233; we cap the report at 0xFFFF anyway,
// matching the engine's u16 dur semantics). On release we reset to 0 so the
// next press starts fresh.
static uint16_t g_h2_held_ticks[256] = {};
static bool h2_vk_pressed_in_halox(uint16_t v, const uint8_t* kb, int btn) {
	bool pressed = false;
	switch (v) {
		case 0x01: pressed = (btn & 0x01) != 0; break;
		case 0x02: pressed = (btn & 0x02) != 0; break;
		case 0x04: pressed = (btn & 0x04) != 0; break;
		case 0x05: pressed = (btn & 0x08) != 0; break;
		case 0x06: pressed = (btn & 0x10) != 0; break;
		default:   pressed = kb[v] != 0;       break;
	}
	if (!pressed) {
		// Combined-modifier folding: engine asks for combined SHIFT/CTRL/ALT,
		// we synthesize from the side-specific keys we tracked.
		if      (v == 0x10) pressed = kb[0xA0] || kb[0xA1];
		else if (v == 0x11) pressed = kb[0xA2] || kb[0xA3];
		else if (v == 0x12) pressed = kb[0xA4] || kb[0xA5];
	}
	return pressed;
}

static unsigned long long __fastcall h2_is_key_held_detour(short vk) {
	// Halox overlay gate — when the pause menu / shell is up, the user is
	// interacting with imgui, not the game. Returning 0 here kills all
	// keyboard / mouse / synthetic-axis input the binding dispatcher and
	// movement code would otherwise read from halox's state mirror.
	if (g_show_imgui) return 0ULL;
	uint16_t v = (uint16_t)vk;
	if (g_remap_armed && v < 256) {
		uint16_t remapped = g_vk_remap[v];
		if (remapped != v) {
			vk = (short)remapped;
			v  = remapped;
		}
	}

	// Authoritative source: halox's win32_input state. The original
	// FUN_1806D1620 calls GetKeyState / GetAsyncKeyState which run on
	// halo2's game thread — that thread never receives WM_LBUTTONDOWN /
	// WM_KEYDOWN (those land on halox's UI/wndproc thread), so its thread-
	// local input state stays empty and the poller sees no presses.
	// Returning halox's tracked state here bypasses that thread-isolation
	// issue entirely.
	//
	// Return value semantics: a held-tick count. Many callers just check
	// "non-zero == held"; some scale by it. Use halox's per-VK held count
	// directly so longer holds report higher values, matching the engine's
	// own dur[] semantics.
	if (v < 256) {
		uint8_t kb[256] = {};
		int btn = win32_input_peek_held(kb);
		bool pressed = h2_vk_pressed_in_halox(v, kb, btn);
		if (pressed) {
			// Engine treats this as a u8 held-tick count; the only callers
			// that read it (movement, pause menu, UI nav) just check non-zero.
			// 1 is sufficient — they don't compare against thresholds.
			return 1ULL;
		}
		return 0ULL;
	}

	if (!g_is_key_held_orig) return 0;
	return g_is_key_held_orig(vk);
}

// FUN_1806D16F0(short vk) — reads the dur (u16) array. Called only by the
// binding-table dispatcher (FUN_1806D8AB0). This is the path used by
// fire / zoom / melee / crouch / jump / reload / etc. — every action whose
// libmcc abstract button maps through halo2's binding table.
//
// We must return a value > the per-action threshold (max seen live = 233)
// so the dispatcher's comparison `(int)threshold < (int)dur` evaluates true.
// We track per-VK held-frame counts and saturate at 0xFFFF, matching the
// engine's own dur semantics. On release the counter resets to 0.
static unsigned long long __fastcall h2_dur_read_detour(short vk) {
	if (g_show_imgui) return 0ULL;
	uint16_t v = (uint16_t)vk;
	uint16_t v_in = v;  // legacy log var (no longer pre-remap; kept for log call)

	// Intentionally NO g_vk_remap here. The binding-table writer
	// (h2_apply_binding_table_overrides) already stamps slot 0 with the user's
	// chosen VK, so the dispatcher polls the user's key directly via dur_read.
	// Applying the FUN_1806D5FA0-direction remap here would double-substitute:
	// e.g. remap[G]=Q (user's throwgrenade) would make ALL dur_read(G) calls
	// read Q's state — which silently breaks any other action_id whose slot 0
	// keeps the engine's G default (notably switchgrenade).

	if (v < 256) {
		uint8_t kb[256] = {};
		int btn = win32_input_peek_held(kb);
		bool pressed = h2_vk_pressed_in_halox(v, kb, btn);
		if (pressed) {
			uint32_t t = (uint32_t)g_h2_held_ticks[v] + 1;
			if (t > 0xFFFFu) t = 0xFFFFu;
			g_h2_held_ticks[v] = (uint16_t)t;
			// Targeted diagnostic: log E only on rising edge and once per
			// 60-call milestone so we can see (a) whether engine is querying
			// dur(E) at all, (b) whether kb[E] is set, (c) what counter we
			// return. Will reveal if hold-E is failing at input layer or
			// further up the dispatcher.
			if (v_in == 0x45 || v == 0x45) {
				uint16_t prev_t = (uint16_t)(t - 1);
				if (prev_t == 0 || (g_h2_held_ticks[v] % 60) == 0) {
					CONSOLE_LOG_INFO("[h2 E-probe] dur_read(vk_in=0x%X→vk=0x%X) pressed=1 kb[E]=%d ticks=%u",
						v_in, v, (int)kb[0x45], (unsigned)g_h2_held_ticks[v]);
				}
			}
			return (unsigned long long)g_h2_held_ticks[v];
		}
		// Falling edge for E: log when ticks drops to zero (i.e. user released).
		if ((v_in == 0x45 || v == 0x45) && g_h2_held_ticks[v] != 0) {
			CONSOLE_LOG_INFO("[h2 E-probe] dur_read(vk_in=0x%X→vk=0x%X) released kb[E]=%d (was held %u ticks)",
				v_in, v, (int)kb[0x45], (unsigned)g_h2_held_ticks[v]);
		}
		g_h2_held_ticks[v] = 0;
		return 0ULL;
	}

	// Synthetic axis tags (0x101..0x104) and out-of-range — defer to the
	// engine. Those are mouse/wheel deltas we don't override.
	if (!g_dur_read_orig) return 0;
	return g_dur_read_orig(vk);
}

static void __fastcall h2_poll_detour(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	if (g_poll_orig) g_poll_orig(a, b, c, d);
	// State-array force-stamp from win32_input was tried but caused freezes
	// during campaign load (likely racing engine writes during init, or
	// disrupting the poller's own internal state). Reverted — only do the
	// VK→VK mirror, which is read-only against win32_input and only writes
	// to halo2's state arrays after the engine's own poll has populated them.
	if (g_remap_armed) mirror_state_arrays();
}

// Logs only on rising edge per (player, action_id), so each press of a key
// produces one log line per action_id that key is bound to — even if many
// frames see the action active. Player 0 only.
static unsigned long long __fastcall h2_action_lookup_detour(
	uintptr_t player, uintptr_t action_id, uintptr_t c, uintptr_t d) {
	auto rv = g_action_lookup_orig ? g_action_lookup_orig(player, action_id, c, d) : 0;
	if ((int)player == 0 && action_id < 256) {
		bool active_now = (rv != 0);
		uint8_t prev = g_h2_action_last_active[action_id];
		if (active_now && !prev) {
			CONSOLE_LOG_INFO("[h2 inst] action_id=%d rising (rv=0x%llx)", (int)action_id, (unsigned long long)rv);
		}
		g_h2_action_last_active[action_id] = active_now ? 1 : 0;
	}
	return rv;
}

void halo2_uninstall_input_hooks() {
	if (!g_hooked_module) return;
	auto base = reinterpret_cast<uintptr_t>(g_hooked_module);

	struct s_hook_target { uintptr_t rva; const char* name; };
	const s_hook_target targets[] = {
		{ k_h2_is_key_held_rva, "is_key_held" },
		{ k_h2_poller_rva,      "poller"      },
		{ k_h2_dur_read_rva,    "dur_read"    },
	};
	for (auto& t : targets) {
		void* target = reinterpret_cast<void*>(base + t.rva);
		__try {
			MH_DisableHook(target);
			MH_RemoveHook(target);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			CONSOLE_LOG_WARN("halo2 input hook uninstall (%s @ %p): SEH 0x%08lX",
				t.name, target, GetExceptionCode());
		}
	}
	CONSOLE_LOG_INFO("halo2 input hooks uninstalled (mod=%p)", g_hooked_module);
	g_is_key_held_orig   = nullptr;
	g_poll_orig          = nullptr;
	g_dur_read_orig      = nullptr;
	g_action_lookup_orig = nullptr;
	g_hooked_module      = nullptr;
}

static void install_is_key_held_hook(HMODULE mod) {
	if (g_hooked_module == mod) return;
	if (g_hooked_module != nullptr) {
		// Stale install from a previous launch (DLL was cycled on quit).
		// Caller should have invoked halo2_uninstall_input_hooks() before
		// unload_modules; if not, the OLD module's address is now stale and
		// MH_RemoveHook would touch unmapped memory. Drop our bookkeeping
		// and proceed to install against the new module — MinHook's internal
		// table for the dead address will be orphaned but never referenced
		// (the install targets here use the FRESH `mod`).
		CONSOLE_LOG_WARN("halo2 input hook: stale g_hooked_module=%p detected "
			"(new mod=%p) — orphaning prior MinHook records and re-installing. "
			"Call halo2_uninstall_input_hooks() before unload_modules to avoid this.",
			g_hooked_module, mod);
		g_is_key_held_orig   = nullptr;
		g_poll_orig          = nullptr;
		g_dur_read_orig      = nullptr;
		g_action_lookup_orig = nullptr;
		g_hooked_module      = nullptr;
	}

	if (!g_minhook_initialized) {
		MH_STATUS st = MH_Initialize();
		if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			CONSOLE_LOG_WARN("halo2 input hook: MH_Initialize failed (status=%d) — KB/M overrides disabled", (int)st);
			return;
		}
		g_minhook_initialized = true;
	}

	void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mod) + k_h2_is_key_held_rva);
	void* trampoline = nullptr;
	MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&h2_is_key_held_detour), &trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_CreateHook @ %p failed (status=%d) — KB/M overrides disabled", target, (int)st);
		return;
	}
	g_is_key_held_orig = reinterpret_cast<IsKeyHeldFn>(trampoline);

	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_EnableHook @ %p failed (status=%d) — KB/M overrides disabled", target, (int)st);
		g_is_key_held_orig = nullptr;
		return;
	}
	g_hooked_module = mod;
	CONSOLE_LOG_INFO("halo2 input hook installed @ %p (halo2+0x%llX)", target, (unsigned long long)k_h2_is_key_held_rva);

	// Second hook: poller. Catches direct-array readers that bypass IsKeyHeld.
	void* poll_target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mod) + k_h2_poller_rva);
	void* poll_tramp  = nullptr;
	MH_STATUS pst = MH_CreateHook(poll_target, reinterpret_cast<void*>(&h2_poll_detour), &poll_tramp);
	if (pst != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_CreateHook(poller) @ %p failed (status=%d) — direct-array readers won't see remap", poll_target, (int)pst);
		return;
	}
	g_poll_orig = reinterpret_cast<PollFn>(poll_tramp);
	pst = MH_EnableHook(poll_target);
	if (pst != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_EnableHook(poller) @ %p failed (status=%d)", poll_target, (int)pst);
		g_poll_orig = nullptr;
		return;
	}
	CONSOLE_LOG_INFO("halo2 poller hook installed @ %p (halo2+0x%llX)", poll_target, (unsigned long long)k_h2_poller_rva);

	// Third hook: dur-array reader (FUN_1806D16F0). The binding-table
	// dispatcher (FUN_1806D8AB0) uses this for every type-1 (kb/mouse)
	// binding — fire / zoom / melee / crouch / jump / reload / etc. The
	// FUN_1806D1620 hook above only catches direct-VK callers (movement,
	// menu nav). Without this hook the binding dispatcher reads the dur
	// array, which is filled by halo2's own poller calling GetAsyncKeyState
	// on the game thread — that path can fail to see WM_LBUTTONDOWN events
	// that halox's wndproc has already consumed, leaving fire = always 0.
	void* dur_target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mod) + k_h2_dur_read_rva);
	void* dur_tramp  = nullptr;
	MH_STATUS dst = MH_CreateHook(dur_target, reinterpret_cast<void*>(&h2_dur_read_detour), &dur_tramp);
	if (dst != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_CreateHook(dur_read) @ %p failed (status=%d) — binding-table actions (fire/zoom/melee) won't work", dur_target, (int)dst);
		return;
	}
	g_dur_read_orig = reinterpret_cast<DurReadFn>(dur_tramp);
	dst = MH_EnableHook(dur_target);
	if (dst != MH_OK) {
		CONSOLE_LOG_WARN("halo2 input hook: MH_EnableHook(dur_read) @ %p failed (status=%d)", dur_target, (int)dst);
		g_dur_read_orig = nullptr;
		return;
	}
	CONSOLE_LOG_INFO("halo2 dur-read hook installed @ %p (halo2+0x%llX)", dur_target, (unsigned long long)k_h2_dur_read_rva);

	// Diagnostic: action_lookup hook (DISABLED). Logged each action_id's
	// rising edge but appears to interfere with halo2's per-frame queries
	// in a way that broke Anniversary visuals — disabling. If we need this
	// data again, install it briefly and accept the visual side-effect, or
	// rewrite as a probe via the bridge instead of an in-process hook.
}

static HMODULE halo2_module() {
	auto im = game_instance_manager();
	if (!im) return nullptr;
	if (im->get_game() != _module_halo2) return nullptr;
	return GetModuleHandleW(L"halo2.dll");
}

void halo2_apply_native_overrides() {
	HMODULE mod = halo2_module();
	if (!mod) return;

	auto* s = mcc_user_settings();
	if (!s->loaded) return;

	// First-call (and on-reload) initialization: identity-init the remap table
	// and overlay any user bindings on top.
	for (int i = 0; i < 256; ++i) g_vk_remap[i] = (uint16_t)i;

	const auto* kbm_src = s->custom_kbm[_module_halo2];

	bool any_remap = false;
	int remapped_count = 0;

	for (size_t k = 0; k < sizeof(k_halo2_defaults) / sizeof(k_halo2_defaults[0]); ++k) {
		const s_halo2_default_bind& def = k_halo2_defaults[k];
		const auto& entry = kbm_src[def.abstract_button];
		if (entry.abstract_button < 0) continue;

		uint16_t user_vk = (uint16_t)entry.virtual_key_codes[0];
		if (user_vk == 0) continue;
		user_vk = normalize_vk(user_vk);
		if (user_vk == def.default_vk) continue;
		if (user_vk >= 256) continue;

		g_vk_remap[def.default_vk] = user_vk;
		any_remap = true;
		remapped_count++;
	}

	g_remap_armed = any_remap;

	// One-time per-launch summary so the user can see what we wired up.
	static bool s_logged = false;
	if (!s_logged) {
		s_logged = true;
		CONSOLE_LOG_INFO("halo2 KB/M: %d binding(s) remapped via FUN_1806D1620 detour", remapped_count);
	}

	install_is_key_held_hook(mod);

	// --- Binding-table writer path -----------------------------------------
	// Resolve player 0's binding table base lazily on first call. This is just
	// (module_base + 0x15EB7A0). It exists immediately on DLL load — no need
	// to wait for player init — but the engine writes defaults into it on
	// FUN_1806D9D70; our writes are idempotent and survive subsequent reads.
	if (!g_h2_binding_player0) {
		g_h2_binding_player0 = h2_resolve_binding_table_player0(mod);
		if (g_h2_binding_player0) {
			CONSOLE_LOG_INFO("halo2 binding table @ %p (player0)", g_h2_binding_player0);
		}
	}
	if (!g_h2_owned_init) {
		for (int i = 0; i < 60; ++i) g_h2_owned_slot[i] = -1;
		g_h2_owned_init = true;
	}

	// Binding-table writer — uses the action_index map CORRECTED via live
	// MCC halo2 binding-table dump. Slot offset bug (+8 → +4) also fixed.
	h2_apply_binding_table_overrides();

	// (Per-frame DAT_1815F1E30 poll removed — it was reading 192 bytes which
	// went past the valid 60-action range into adjacent random-byte regions
	// that flicker every frame. The resulting log spam caused I/O backpressure
	// and froze the game thread mid-session. The data we got from it is now
	// captured in the action-id mapping above (action 6 = reload, action 31 =
	// flashlight). If we need the diagnostic again, restrict it to a < 60.)

	// NOTE: stamp_state_from_win32_input() is intentionally NOT called here.
	// It was previously tried in h2_poll_detour and caused freezes during
	// campaign load. The replacement is in h2_is_key_held_detour itself —
	// that function now returns halox's tracked input directly, bypassing
	// the GetKeyState-on-game-thread blackhole that was the root cause of
	// LMB / crouch / etc not registering.

	// One-time diagnostic: dump every action's count + slot 0 (type, key_id) so
	// we can see exactly what halo2 has stored. Lets us identify the action
	// indices for switchgrenade / switchweapon (currently absent from the
	// k_h2_action_table) and verify fire (10) / crouch (2) entries are intact.
	// Logs once per launch ~3 seconds after first apply (lets engine pre-init
	// run + halox's own writes settle).
	static bool   s_diag_logged = false;
	static double s_diag_t0     = 0.0;
	if (!s_diag_logged && g_h2_binding_player0) {
		LARGE_INTEGER now, freq;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&freq);
		double t = (double)now.QuadPart / (double)freq.QuadPart;
		if (s_diag_t0 == 0.0) s_diag_t0 = t;
		if (t - s_diag_t0 > 3.0) {
			s_diag_logged = true;
			CONSOLE_LOG_INFO("halo2 binding-table DIAGNOSTIC dump (player0 @ %p):", g_h2_binding_player0);
			for (int a = 0; a < 60; ++a) {
				uintptr_t off = k_h2_action_table_off + (uintptr_t)a * k_h2_action_stride;
				int32_t* p = reinterpret_cast<int32_t*>(g_h2_binding_player0 + off);
				int32_t count = p[0];
				if (count <= 0 || count > 8) continue;
				// Dump up to all 8 slots (full picture — single slot 0 wasn't
				// enough to diagnose the reload/flashlight on-Z dual-fire bug).
				char buf[256];
				int n = snprintf(buf, sizeof(buf), "  action[%2d] count=%d", a, count);
				for (int s = 0; s < count && s < 8; ++s) {
					int32_t* sp = p + 1 + s * 3;
					n += snprintf(buf + n, sizeof(buf) - n,
						" s%d={t=%d k=0x%02X th=%d}",
						s, sp[0], (unsigned)sp[1], sp[2]);
					if (n >= (int)sizeof(buf) - 32) break;
				}
				CONSOLE_LOG_INFO("%s", buf);
			}
			CONSOLE_LOG_INFO("halo2 binding-table DIAGNOSTIC end");
		}
	}
}

// --- Campaign pause-menu actions ------------------------------------------

static constexpr uintptr_t k_h2_revert_fn_rva          = 0x67A950;
// Helper-function entry points for restart-mission. Direct flag-writes were
// tried first but the game crashes — the pump expects internal state set up
// by these helpers (event listeners / scenario validation) before consuming
// the flags. Calling FUN_180866B80(1) sets the restart-pending byte AND
// queues the global-event handler; FUN_18067A980 sets DAT_180E70D7F so the
// per-tick pump (FUN_180679D50) picks it up and runs FUN_180037040.
static constexpr uintptr_t k_h2_restart_pending_fn_rva = 0x866B80;  // FUN_180866B80
static constexpr uintptr_t k_h2_restart_dispatch_rva   = 0x67A980;  // FUN_18067A980

void halo2_revert_to_checkpoint() {
	HMODULE h = GetModuleHandleW(L"halo2.dll");
	if (!h) {
		CONSOLE_LOG_WARN("halo2_revert_to_checkpoint: halo2.dll not loaded");
		return;
	}
	auto fn = reinterpret_cast<void(*)()>((uintptr_t)h + k_h2_revert_fn_rva);
	__try {
		fn();
		CONSOLE_LOG_INFO("halo2_revert_to_checkpoint: queued (FUN_18067A950 called)");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		CONSOLE_LOG_ERROR("halo2_revert_to_checkpoint: SEH 0x%08lX in FUN_18067A950",
			GetExceptionCode());
	}
}

// Audio kill — RVAs from RE agent (2026-04-29).
static constexpr uintptr_t k_h2_sound_disabled_byte_rva = 0x15F21C8;  // DAT_1815F21C8
static constexpr uintptr_t k_h2_stop_all_channels_rva   = 0x6CA4C0;   // FUN_1806CA4C0
static constexpr uintptr_t k_h2_xaudio2_global_rva      = 0x1E91248;  // DAT_181E91248 (IXAudio2*)

void halo2_silence_audio() {
	HMODULE h = GetModuleHandleW(L"halo2.dll");
	if (!h) return;
	auto base = (uintptr_t)h;

	__try {
		// (1) Block new sounds from starting (every audio entry checks
		// this flag in FUN_1806E6100).
		*reinterpret_cast<uint8_t*>(base + k_h2_sound_disabled_byte_rva) = 1;

		// (2) Stop every active channel via halo2's own stop-all helper.
		// This iterates DAT_1815E4B88's channel array and AIL_end_sample
		// + releases each.
		auto stop_all = reinterpret_cast<void(*)()>(base + k_h2_stop_all_channels_rva);
		stop_all();

		// (3) Stop the XAudio2 engine for any video / Bink playback. The
		// IXAudio2 instance pointer lives at +0x1E91248; vtable+0x48 is
		// StopEngine. Null-check first — the engine isn't always created.
		void** xaudio2_pp = reinterpret_cast<void**>(base + k_h2_xaudio2_global_rva);
		void*  xaudio2 = *xaudio2_pp;
		if (xaudio2) {
			void* vtable = *reinterpret_cast<void**>(xaudio2);
			if (vtable) {
				auto stop_engine = *reinterpret_cast<void(__fastcall**)(void*)>(
					reinterpret_cast<uint8_t*>(vtable) + 0x48);
				if (stop_engine) stop_engine(xaudio2);
			}
		}

		CONSOLE_LOG_INFO("halo2_silence_audio: blocked new sounds + stopped channels + StopEngine");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		CONSOLE_LOG_ERROR("halo2_silence_audio: SEH 0x%08lX", GetExceptionCode());
	}
}

void halo2_restart_mission() {
	HMODULE h = GetModuleHandleW(L"halo2.dll");
	if (!h) {
		CONSOLE_LOG_WARN("halo2_restart_mission: halo2.dll not loaded");
		return;
	}
	auto base = (uintptr_t)h;
	auto pending_fn  = reinterpret_cast<void(*)(int)>(base + k_h2_restart_pending_fn_rva);
	auto dispatch_fn = reinterpret_cast<void(*)()>   (base + k_h2_restart_dispatch_rva);
	__try {
		// Stage 1: queue the restart-pending event with the global handler.
		// The (1) parameter selects "restart-mission" out of the function's
		// switched event types.
		pending_fn(1);
		// Stage 2: set the consumer flag the per-tick pump checks. Pump
		// then drives FUN_180037040 for the full scenario reload.
		dispatch_fn();
		CONSOLE_LOG_INFO("halo2_restart_mission: queued (FUN_180866B80(1) + FUN_18067A980 called)");
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		CONSOLE_LOG_ERROR("halo2_restart_mission: SEH 0x%08lX in restart helper calls",
			GetExceptionCode());
	}
}
