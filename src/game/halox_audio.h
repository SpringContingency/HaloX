#pragma once

#include <libmcc/libmcc.h>

// halox audio control. Wraps each engine's audio middleware so the in-engine
// settings UI can drive volumes uniformly. Volumes are 0..100 ints; 100 is
// unattenuated and 0 is silent.
//
// Backends per game:
//   halo1, halo2 — Miles Sound System (mss64.dll). Volume is set on the
//                  primary digital driver via AIL_set_digital_master_volume_level.
//   halo3, halo4, groundhog, halo3odst, haloreach — Wwise (TBD; not yet wired).
//
// The functions are safe to call before the audio engine is up; they no-op
// until the underlying APIs become resolvable. Conversely, calling after the
// game has shut down audio is also safe (returns false, doesn't crash).
namespace halox::audio {

// Apply all audio settings for the given game from the singleton
// mcc_user_settings()->audio[module]. Called when the user moves a slider
// AND once on game launch so the configured volume is in effect from the
// first audible frame.
//
// Returns true if at least one knob was successfully written. A false return
// means the audio engine isn't reachable yet (game not running, middleware
// not initialized) — try again later.
bool apply_audio_for_module(libmcc::e_module module);

// Convenience: apply just the master volume for a game. Equivalent to
// reading audio[module].master_volume and pushing it through the right
// backend.
bool set_master_volume_pct(libmcc::e_module module, int pct);

}  // namespace halox::audio
