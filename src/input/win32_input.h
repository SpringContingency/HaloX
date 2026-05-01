#pragma once

#include <Windows.h>
#include <libmcc/input/input.h>

// Capture pumps keyboard/mouse state from window-procedure messages into a
// thread-safe snapshot that the game-side input callbacks can read. Player 0
// is always the local windowed player.

// Register the window for WM_INPUT raw mouse delivery. Call once after the
// window is created. Mouse deltas come from WM_INPUT (true relative counts);
// WM_MOUSEMOVE is only used for the absolute cursor position (menu hit-test).
bool win32_input_register_raw_mouse(HWND hwnd);

void win32_input_dispatch(UINT msg, WPARAM wParam, LPARAM lParam, HWND hwnd);

// Copies the current keyboard/mouse snapshot. Mouse delta fields (lX/lY/lZ)
// are consumed (zeroed) by the snapshot so the next call returns only what's
// arrived since.
void win32_input_snapshot(libmcc::s_keyboard_state* keyboard,
                          libmcc::s_mouse_state* mouse);

// Cheap check: any key currently held? Used to decide is_km without locking.
bool win32_input_keyboard_active();

// Non-consuming read of the held-VK array (256 entries, byte each = 0/1) and
// the mouse-button bitmask. Unlike win32_input_snapshot, this does NOT consume
// mouse deltas, so it's safe to call alongside the gameplay snapshot reader.
// `kb_out` may be null. Returns mouse button bitmask:
//   bit 0 = LMB (VK_LBUTTON 0x01), bit 1 = RMB (0x02), bit 2 = MMB (0x04),
//   bit 3 = X1 (0x05), bit 4 = X2 (0x06).
int win32_input_peek_held(uint8_t kb_out[256]);

// Sticky "the user is using kbm right now" flag. Set whenever keyboard or
// mouse input arrives; cleared when the gamepad reports activity. Used so
// is_km doesn't flicker per frame while the user holds the mouse still.
bool win32_input_kbm_sticky_active();
void win32_input_clear_kbm_sticky();

// Discard any accumulated raw-mouse delta without producing a snapshot.
// Use when the game's tick loop is paused (overlay up / engine paused), to
// prevent a single huge delta from being delivered the moment the loop
// resumes — without this, the camera "shoots" to wherever the user moved
// the mouse during the pause.
void win32_input_drain_deltas();

// User-tunable mouse sensitivity multiplier. 1.0 = baseline. Read by the
// snapshot function and editable from the launcher's Input settings panel.
float win32_input_get_mouse_sensitivity();
void  win32_input_set_mouse_sensitivity(float v);

// When true, mouse Y delta is negated before being handed to the game.
bool win32_input_get_invert_y();
void win32_input_set_invert_y(bool v);

// Tell the input layer whether the OS cursor is currently clipped/hidden
// (i.e. apply_cursor_capture(true) is in effect). When true, raw-mouse
// motion synthesizes a SetCursorPos that drags the OS cursor along the
// virtual cursor's position inside the client rect — required so that
// game code reading GetCursorPos directly (Reach's loadout / pause-menu
// hit-testing, halo3 forge palette equivalents, etc.) sees a moving
// pointer instead of the cursor pinned at the clip-rect edge.
//
// Pass the HWND that owns the client rect; null disables the sync.
void win32_input_set_cursor_capture(HWND hwnd, bool captured);
