#pragma once

#include <Windows.h>

// Launch state machine — keeps the message-pump thread responsive while a
// game DLL initializes.
//
// Flow:
//   1. UI button → c_game_instance_manager::launch_game() → posts
//      _window_message_game_launch.
//   2. process_message() handler calls ui_launch_kick(): hides imgui, sets
//      g_launch_in_progress=1, spawns a worker thread that runs the existing
//      launch_game_internal() call.
//   3. Main loop sees g_launch_in_progress=1 and switches to the loading
//      screen render (keeps message pump alive).
//   4. Worker calls launch_game_internal() — which updates the status text
//      via ui_launch_set_status() at each step. When done, worker writes the
//      result code, clears g_launch_in_progress.
//   5. Main loop notices flag clear; if launch failed (no game thread),
//      restores g_show_imgui = TRUE so the user sees the launcher again with
//      the failure code displayed.
//
// All shared state is plain volatile + Interlocked* — no CRITICAL_SECTION on
// the hot path. The status string uses a single producer / single consumer
// pattern (worker writes, UI reads) which is fine for diagnostic display.

namespace halox::ui {

// Atomic flag — set while a launch worker is running.
extern volatile LONG g_launch_in_progress;

// Result code from the most recent launch attempt. -999 = none yet.
// 0 = success (game thread alive). Other values = launch_game_internal rc.
extern volatile LONG g_last_launch_rc;

// Update the status line shown on the loading screen and (if window_handle
// is set) in the window title. Safe to call from any thread.
void ui_launch_set_status(const char* fmt, ...);

// Read the current status into a caller-provided buffer (NUL-terminated).
void ui_launch_get_status(char* dst, int dst_size);

// Called from process_message on _window_message_game_launch. Spawns a worker
// thread that runs launch_game_internal asynchronously. Returns immediately.
// Idempotent: if a launch is already in progress, this is a no-op.
void ui_launch_kick();

// Called once per main-loop iteration BEFORE the rasterizer touches D3D.
// If a launch is in progress, this:
//   - keeps the message pump active by yielding
//   - returns true to instruct the caller to skip its normal render path
//     (the worker thread holds the D3D context for game DLL inits)
// If no launch is in progress, returns false and (if a launch just finished
// with a failure) restores the imgui menu.
bool ui_launch_pump();

// Render the loading screen — fullscreen panel with animated ring + status
// text. Called from the main loop when ui_launch_pump() returned true AND
// the rasterizer is safe to use (i.e. no in-progress launch). Currently we
// don't render during in-progress to avoid D3D contention; this entry point
// is reserved for the post-thread-locked design.
void ui_launch_render_overlay();

}  // namespace halox::ui
