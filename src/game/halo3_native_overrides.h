#pragma once

// Halo 3 specific: stamp MCC user settings (KB/M bindings, FOV) directly into
// halo3.dll's per-player profile cache.
//
// Why this exists: halo3.dll does NOT call i_game_manager::get_player_profile.
// It populates its own per-player cache at halo3.dll+0x2D3ED70 (4 entries x
// 0xD78) and reads from there. So writing libmcc::s_player_profile fields via
// our c_game_manager has no effect on halo3 sensitivity / FOV / bindings.
//
// Instead we wait until halo3 has populated each entry (flags bit 2 = valid),
// then overlay our values per-frame. Mouse sensitivity is a separate path
// (halo3 calls our get_input_state every frame and uses lX/lY directly), so
// it's handled in win32_input — only FOV + bindings live here.
void halo3_apply_native_overrides();
