#include "halo3_native_overrides.h"

#include "game_instance_manager.h"
#include "mcc_user_settings.h"
#include "../logging/logging.h"

#include <Windows.h>
#include <cstdint>

using namespace libmcc;

// --- halo3.dll cache layout (RE'd via ReverseMe; see memory note
// "halo3 ignores i_game_manager::get_player_profile") --------------------

static constexpr uintptr_t k_h3_cache_rva       = 0x2D3ED70;  // per-player profile cache base
static constexpr size_t    k_h3_cache_stride    = 0xD78;       // bytes per entry
static constexpr int       k_h3_cache_count     = 4;           // local players
static constexpr size_t    k_h3_off_flags       = 0x00;        // bit 2 = "valid"
static constexpr size_t    k_h3_off_fov_deg     = 0x74;        // float, vertical FOV in degrees
static constexpr size_t    k_h3_off_kbm_table   = 0x20C;       // u16 x 75, stride 4 (abstract -> VK)
                                                                // (corrected from 0x214 via FUN_1802FB8E0 / FUN_1802FBA40 decompile)
static constexpr size_t    k_h3_off_kbm_secondary = 0x338;     // byte x 67, stride 4 (modifiers)
static constexpr int       k_h3_kbm_slot_count  = 75;
static constexpr uint8_t   k_h3_flags_valid_bit = 0x4;

// libmcc::e_game_abstract_button -> halo3 internal slot (0..74).
//
// PLACEHOLDER. ReverseMe agent (a53f035e4246c11b0) is currently producing
// the verified mapping by decompiling FUN_1802FB8E0 / FUN_180188594 in
// halo3.dll. Until that lands, every entry is -1 (skipped) so we don't
// silently corrupt halo3's bindings. Replace this table with the agent's
// deliverable.
static const int8_t k_libmcc_to_halo3_abstract_button[k_game_abstract_button_count] = {
	/* _game_abstract_button_jump                                  */  -1,
	/* _game_abstract_button_switchgrenade                         */  -1,
	/* _game_abstract_button_actionreload                          */  -1,
	/* _game_abstract_button_reload                                */  -1,
	/* _game_abstract_button_switchweapon                          */  -1,
	/* _game_abstract_button_meleeattack                           */  -1,
	/* _game_abstract_button_flashlight                            */  -1,
	/* _game_abstract_button_throwgrenade                          */  -1,
	/* _game_abstract_button_fire                                  */  -1,
	/* _game_abstract_button_crouch                                */  -1,
	/* _game_abstract_button_zoom                                  */  -1,
	/* _game_abstract_button_zoomin                                */  -1,
	/* _game_abstract_button_zoomout                               */  -1,
	/* _game_abstract_button_swapweapon                            */  -1,
	/* _game_abstract_button_sprint                                */  -1,
	/* _game_abstract_button_bansheebomb                           */  -1,
	/* _game_abstract_button_moveforward                           */  -1,
	/* _game_abstract_button_movebackward                          */  -1,
	/* _game_abstract_button_strafeleft                            */  -1,
	/* _game_abstract_button_straferight                           */  -1,
	/* _game_abstract_button_showscores                            */  -1,
	/* _game_abstract_button_primaryvehicletrick                   */  -1,
	/* _game_abstract_button_secondaryvehicletrick                 */  -1,
	/* _game_abstract_button_equipment                             */  -1,
	/* _game_abstract_button_secondaryfire                         */  -1,
	/* _game_abstract_button_lifteditor                            */  -1,
	/* _game_abstract_button_dropeditor                            */  -1,
	/* _game_abstract_button_grabobjecteditor                      */  -1,
	/* _game_abstract_button_boosteditor                           */  -1,
	/* _game_abstract_button_croucheditor                          */  -1,
	/* _game_abstract_button_deleteobjecteditor                    */  -1,
	/* _game_abstract_button_createobjecteditor                    */  -1,
	/* _game_abstract_button_opentoolmenueditor                    */  -1,
	/* _game_abstract_button_switchplayermodeeditor                */  -1,
	/* _game_abstract_button_scopezoomeditor                       */  -1,
	/* _game_abstract_button_playerlockformanipulationeditor       */  -1,
	/* _game_abstract_button_showhidepanneltheater                 */  -1,
	/* _game_abstract_button_showhideinterfacetheater              */  -1,
	/* _game_abstract_button_togglefirstthirdpersonviewtheater     */  -1,
	/* _game_abstract_button_camerafocustheater                    */  -1,
	/* _game_abstract_button_fastforwardtheater                    */  -1,
	/* _game_abstract_button_fastrewindtheater                     */  -1,
	/* _game_abstract_button_stopcontinueplaybacktheater           */  -1,
	/* _game_abstract_button_playbackspeeduptheater                */  -1,
	/* _game_abstract_button_enterfreecameramodetheater            */  -1,
	/* _game_abstract_button_movementspeeduptheater                */  -1,
	/* _game_abstract_button_panningcameratheater                  */  -1,
	/* _game_abstract_button_cameramoveuptheater                   */  -1,
	/* _game_abstract_button_cameramovedowntheater                 */  -1,
	/* _game_abstract_button_dualwield                             */  -1,
	/* _game_abstract_button_zoomcameratheater                     */  -1,
	/* _game_abstract_button_togglerotationaxeseditor              */  -1,
	/* _game_abstract_button_duplicateobjecteditor                 */  -1,
	/* _game_abstract_button_lockobjecteditor                      */  -1,
	/* _game_abstract_button_resetorientationeditor                */  -1,
	/* _game_abstract_button_reloadsecondary                       */  -1,
	/* _game_abstract_button_previousgrenade                       */  -1,
	/* _game_abstract_button_specialaction                         */  -1,
	/* _game_abstract_button_loadoutmenu                           */  -1,
	/* _game_abstract_button_activatewaypoint                      */  -1,
	/* _game_abstract_button_activatewaypointalt                   */  -1,
	/* _game_abstract_button_pingnavpoints                         */  -1,
	/* _game_abstract_button_raisehornet                           */  -1,
	/* _game_abstract_button_lowerhornet                           */  -1,
	/* _game_abstract_button_flashlightalt                         */  -1,
	/* _game_abstract_button_nextgrenade                           */  -1,
};

static HMODULE halo3_module() {
	auto im = game_instance_manager();
	if (!im) return nullptr;
	if (im->get_game() != _module_halo3) return nullptr;
	// halo3 was loaded by game_instance_manager via LoadLibrary; ask Windows
	// for the live handle. Avoids needing a public accessor on the manager.
	return GetModuleHandleW(L"halo3.dll");
}

static bool any_kbm_mapping_set(const s_mcc_user_settings* s) {
	for (int n = 0; n < k_game_abstract_button_count; ++n) {
		if (s->custom_kbm[_module_halo3][n].abstract_button >= 0) return true;
	}
	return false;
}

void halo3_apply_native_overrides() {
	HMODULE mod = halo3_module();
	if (!mod) return;

	auto* s = mcc_user_settings();
	if (!s->loaded) return;

	uintptr_t base  = reinterpret_cast<uintptr_t>(mod);
	uint8_t*  cache = reinterpret_cast<uint8_t*>(base + k_h3_cache_rva);

	// MCC stores halo3's FOV slider in Config\Halo3\preferences.dat (binary
	// blob) — NOT in GameUserSettings.ini[FOVSetting[2]] which is always 0.
	// Until preferences.dat parsing is added, fall back to 120 so the slider
	// actually does something.
	int want_fov_deg = s->fov[_module_halo3];
	if (want_fov_deg <= 0) want_fov_deg = 120;
	const bool  have_kbm     = any_kbm_mapping_set(s);
	const auto* kbm_src      = s->custom_kbm[_module_halo3];

	for (int i = 0; i < k_h3_cache_count; ++i) {
		uint8_t* entry = cache + (size_t)i * k_h3_cache_stride;

		// Wait until halo3 has populated this slot. Stamping before init wipes
		// the whole struct on the next halo3-internal copy.
		uint8_t flags = entry[k_h3_off_flags];
		if (!(flags & k_h3_flags_valid_bit)) continue;

		if (want_fov_deg > 0) {
			*reinterpret_cast<float*>(entry + k_h3_off_fov_deg) = (float)want_fov_deg;
		}

		if (have_kbm) {
			uint16_t* table = reinterpret_cast<uint16_t*>(entry + k_h3_off_kbm_table);
			// Each entry is u16 with stride 4 — that means slot N lives at
			// table[N*2] (skipping a u16 of padding/alt-key per slot).
			for (int n = 0; n < k_game_abstract_button_count; ++n) {
				int8_t h3_slot = k_libmcc_to_halo3_abstract_button[n];
				if (h3_slot < 0 || h3_slot >= k_h3_kbm_slot_count) continue;
				if (kbm_src[n].abstract_button < 0) continue;
				uint16_t vk = (uint16_t)kbm_src[n].virtual_key_codes[0];
				if (vk == 0) continue;
				table[(size_t)h3_slot * 2] = vk;
			}
		}
	}
}
