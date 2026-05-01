#pragma once

// Liveness probe for an in-game frame stream. Reads back a few pixels from
// the rasterizer's game surface each frame, classifies them as black or
// "alive", and emits log lines so external tooling (or you, scrolling
// halox.log) can tell:
//
//   - whether the game's render thread is actually drawing real frames;
//   - whether the launch landed in a black-screen freeze.
//
// Call `liveness_probe_tick()` once per main-loop iteration while in-game.
// The probe self-throttles (samples ~once per second) and avoids any work
// before the first in-game frame. Idempotent and crash-safe (any D3D11
// hiccup just defers the next sample).

void liveness_probe_tick();

// True once the probe has observed at least one non-black sampled frame in
// the current in_game stream. Reset to false when in_game drops.
bool liveness_first_alive_seen();

// True once the probe has seen N consecutive alive samples (no intervening
// black). This is the "load is REALLY done" signal — first_alive_seen by
// itself flips on for a single splash/intro frame and then the engine
// returns to black for the actual scenario-load phase, which would cause
// the loading overlay to disappear prematurely.
bool liveness_stable_alive();
