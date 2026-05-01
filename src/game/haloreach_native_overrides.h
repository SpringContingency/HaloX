#pragma once

#include <cstdint>

// Halo Reach analog of halo3_native_overrides — same architecture, different
// cache offsets. Reach also bypasses i_game_manager::get_player_profile and
// reads from its own per-player cache. We stamp FOV / KB/M bindings into that
// cache per-frame after Reach has populated it.
//
// Cache layout RVAs are pending RE — see project_haloreach_profile_bypass
// memory note. Until then this is a no-op (k_hr_cache_rva == 0).
void haloreach_apply_native_overrides();

// Spawn a one-shot worker thread that polls Reach's launch state machine
// (DAT_180C1A114, RVA 0xC1A114) every 10ms for ~30s and logs every transition.
// Logs all four diagnostic globals (state, film_active, psjs gate, external
// launch gate) at install time and on every state delta. Used to diagnose
// whether ProcessSessionJoinState dispatched campaign through state 4
// (ProcessPlaybackFrame, expected) or state 5 (FinalizePlayback, broken path).
//
// Idempotent across re-launches; first call spawns the thread, subsequent
// calls are no-ops until the thread exits.
void haloreach_start_state_logger();

// Detour FUN_1802a41c4 (haloreach.dll RVA 0x2A41C4) — the scenario
// path-resolve + (conditional) map-load function called from
// ProcessPlaybackFrame at RVA 0xE3B9. Body:
//   FUN_1802a41c4(GUID*, map_id*, run_flag) {
//       FUN_18003db60(GUID, map_id, &str, 0x100);          // resolve path
//       if (str[0] && FUN_180398458(GUID, map_id) && run_flag)
//           FUN_180022058(GUID, map_id);                    // queue load
//   }
// PPF calls this with run_flag=0 (preflight). For the actual campaign load
// to queue, run_flag must be 1. We force it to 1 once per campaign launch
// (one-shot), which catches PPF's first call without affecting the other 9
// callers (FinalizePlayback, film handlers, GameVariant_ReplicationUpdate)
// that may legitimately pass run_flag=0.
//
// Idempotent across re-launches. Install BEFORE initialize_game (same window
// as haloreach_start_state_logger).
void haloreach_install_campaign_load_hook();

// Arm the one-shot run_flag override. Call AFTER the hook is installed and
// BEFORE the engine thread spawns its first PPF tick. Cleared automatically
// inside the detour after the first fire.
void haloreach_arm_campaign_load_force();
