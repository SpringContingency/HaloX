#pragma once

#include <cstdint>

// Per-game live loading-progress reader. Backed by reverse-engineered
// addresses in each game DLL. See agent transcript notes 2026-04-29 in the
// PR description for derivation; key fields:
//
//   halo2.dll  DAT_181E90E70 + 0xAC  float (0..1)   — current progress
//              DAT_181E90E70 + 0x80  byte           — "active" flag
//              DAT_181E90E70 + 0xBC/0xC0  step start/end fractions
//   haloreach  DAT_180C1A114  uint32  — external-launch state enum
//                              0xB = "map-loading-active"
//
// halo3 / halo4 / halo3odst / halo1 / groundhog: no float-percent global
// found — fall back to indeterminate "Loading…" UI for those.

namespace halox::ui {

struct s_progress {
	bool   valid;          // true if any data could be read
	float  fraction;       // 0..1, or -1.0f if unknown (use indeterminate UI)
	char   step[96];       // human-readable current step (e.g. "Loading map", "Initializing")
};

// Sample current loading progress for the active game module. Cheap (a few
// reads in-process). Returns a populated s_progress; check .valid first.
s_progress progress_query();

// True while a load is in progress (worker still running, OR game thread
// alive but scenario not yet loaded). Used to keep the loading overlay
// visible during the long BLACK-frame phase that follows initialize_game.
//
// Detection heuristic per game:
//   halo2     — progress fraction < 0.99
//   haloreach — external-launch state != 4 (Playback)
//   others    — fixed timeout window since game thread start (no progress
//               global available; agent confirmed dead end for halo1 / halo3
//               / halo4 / halo3odst / groundhog)
bool progress_is_loading();

// Called from the launch worker on a successful initialize_game so we know
// when to start counting "elapsed since game thread alive" for the
// timeout-based loading detection (games without a progress global).
void progress_mark_game_thread_started();

// Wall-clock (GetTickCount64) at which liveness first transitioned to
// stable_alive in the current load. Returns 0 if not stable yet. Used by
// the loading-screen renderer to drive the "finalizing — ramp to 100%"
// animation across the post-load black-frame transition window.
uint64_t progress_finalize_started_ms();

// Wall-clock at PLAY click for the current launch (set by
// progress_mark_game_thread_started). Returns 0 between launches. Renderers
// use this as a launch-epoch token: when the value changes, reset any
// per-launch eased state (e.g. the displayed-fraction lerp) so a second
// launch doesn't inherit the previous launch's terminal 100% display.
uint64_t progress_launch_epoch();

}  // namespace halox::ui
