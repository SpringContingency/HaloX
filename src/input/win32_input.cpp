#include "win32_input.h"

#include "../logging/logging.h"

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#include <hidusage.h>  // HID_USAGE_PAGE_GENERIC / HID_USAGE_GENERIC_MOUSE
#include <atomic>

#pragma comment(lib, "User32.lib")

using namespace libmcc;

static SRWLOCK              g_input_lock = SRWLOCK_INIT;
static s_keyboard_state     g_keyboard{};       // current key down state, indexed by VK
static std::atomic<int>     g_keys_held{0};     // count of held keys (lock-free is_km probe)
// Sticky kbm-active flag — once any keyboard/mouse activity happens we stay
// in kbm mode until a gamepad event arrives. Otherwise is_km flickers per
// frame whenever the user holds the mouse still for a tick, which makes
// halo3 alternate kbm/pad input paths and produces jumpy aim.
static std::atomic<bool>    g_kbm_active{false};

// Mouse: relative deltas come from WM_INPUT (raw mouse counts, unaffected by
// cursor clipping / SetCursorPos / DPI). Absolute position is from
// WM_MOUSEMOVE — used only to populate s_mouse_state::pX/pY for menu hit-test.
static float                g_mouse_lX = 0.0f;  // accumulated raw-input delta X
static float                g_mouse_lY = 0.0f;  // accumulated raw-input delta Y
static float                g_mouse_lZ = 0.0f;  // accumulated wheel delta
static int                  g_mouse_pX = 0;     // last absolute client-X
static int                  g_mouse_pY = 0;     // last absolute client-Y
static int                  g_mouse_buttons = 0;

// Virtual cursor — integrates raw-input deltas into an absolute position
// clamped to the window client rect. Required because we hide+clip the OS
// cursor while the game is captured: WM_MOUSEMOVE stops firing once the OS
// cursor is pinned at the clip-rect edge, so g_mouse_pX/pY freezes. The Forge
// tool palette (and other in-game pointer UIs) hit-test against the absolute
// pX/pY, not the lX/lY deltas, so a frozen pX/pY makes the palette uninteractable.
//
// We integrate raw deltas (which keep arriving regardless of clipping) into
// g_virt_cursor_*, clamped to the latest known client size. When the OS cursor
// is unclipped and WM_MOUSEMOVE is firing, the WM_MOUSEMOVE handler also
// re-syncs the virtual cursor so it follows the real one.
static float                g_virt_cursor_x = 0.0f;
static float                g_virt_cursor_y = 0.0f;
static int                  g_client_w = 1;
static int                  g_client_h = 1;

// When the OS cursor is captured (hidden + clipped to the client rect) the
// raw-input handler also drives SetCursorPos so that the OS cursor follows
// our virtual cursor inside the rect. Without this, GetCursorPos reads a
// stale/edge-pinned position and any game UI that hit-tests against
// GetCursorPos (Reach's pause/loadout menu uses GetCursorPos+ScreenToClient
// directly) becomes uninteractable. The HWND is needed to convert client
// coords back to screen coords for SetCursorPos.
static std::atomic<HWND>    g_capture_hwnd{nullptr};
static std::atomic<bool>    g_cursor_captured{false};
// Re-entrancy guard: SetCursorPos generates a WM_MOUSEMOVE that lands in
// our dispatcher and would overwrite the just-set virtual cursor. We
// suppress that single message by tagging the screen coords we just set.
static std::atomic<int>     g_synth_cursor_x{INT_MIN};
static std::atomic<int>     g_synth_cursor_y{INT_MIN};

static void set_button(int bit, bool down) {
	AcquireSRWLockExclusive(&g_input_lock);
	if (down) g_mouse_buttons |=  (1 << bit);
	else      g_mouse_buttons &= ~(1 << bit);
	ReleaseSRWLockExclusive(&g_input_lock);
}

bool win32_input_register_raw_mouse(HWND hwnd) {
	RAWINPUTDEVICE rid{};
	rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
	rid.usUsage     = HID_USAGE_GENERIC_MOUSE;
	rid.dwFlags     = RIDEV_INPUTSINK;  // receive input even when not foreground
	rid.hwndTarget  = hwnd;
	return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

static void handle_raw_input(LPARAM lParam) {
	UINT size = 0;
	GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
	if (size == 0 || size > 256) return;
	BYTE buf[256];
	if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size) return;
	auto* ri = reinterpret_cast<RAWINPUT*>(buf);
	if (ri->header.dwType != RIM_TYPEMOUSE) return;

	// Relative motion is the typical case; ignore absolute (tablet/RDP).
	if ((ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
		const LONG dx = ri->data.mouse.lLastX;
		const LONG dy = ri->data.mouse.lLastY;
		int virt_x_int = 0, virt_y_int = 0;
		AcquireSRWLockExclusive(&g_input_lock);
		g_mouse_lX += (float)dx;
		g_mouse_lY += (float)dy;
		// Drive the virtual cursor too, so pX/pY stays alive while the OS
		// cursor is hidden + clipped (WM_MOUSEMOVE goes silent in that state).
		// Use the same raw counts directly (not the gameplay-sensitivity-scaled
		// values) so menu cursor speed feels like a real desktop mouse rather
		// than the very-low aim sensitivity used for camera look.
		g_virt_cursor_x += (float)dx;
		g_virt_cursor_y += (float)dy;
		if (g_virt_cursor_x < 0.0f)              g_virt_cursor_x = 0.0f;
		if (g_virt_cursor_y < 0.0f)              g_virt_cursor_y = 0.0f;
		if (g_virt_cursor_x > (float)g_client_w) g_virt_cursor_x = (float)g_client_w;
		if (g_virt_cursor_y > (float)g_client_h) g_virt_cursor_y = (float)g_client_h;
		virt_x_int = (int)g_virt_cursor_x;
		virt_y_int = (int)g_virt_cursor_y;
		ReleaseSRWLockExclusive(&g_input_lock);
		if (dx || dy) g_kbm_active.store(true, std::memory_order_relaxed);

		// While the OS cursor is hidden + clipped, drag it along with the
		// virtual cursor so game code that reads GetCursorPos sees a live
		// pointer rather than the cursor pinned at the clip-rect edge.
		// Reach's pause/loadout menus call GetCursorPos+ScreenToClient
		// directly (FUN_1802d2018, FUN_18034e774 in haloreachnew) and
		// would otherwise hit-test against a frozen screen position.
		if (g_cursor_captured.load(std::memory_order_acquire) && (dx || dy)) {
			HWND hwnd = g_capture_hwnd.load(std::memory_order_acquire);
			if (hwnd) {
				POINT pt{ virt_x_int, virt_y_int };
				if (ClientToScreen(hwnd, &pt)) {
					// Tag the synthesized position so the WM_MOUSEMOVE we'll
					// generate doesn't get folded back into g_virt_cursor_*.
					g_synth_cursor_x.store(pt.x, std::memory_order_release);
					g_synth_cursor_y.store(pt.y, std::memory_order_release);
					SetCursorPos(pt.x, pt.y);
				}
			}
		}
	}
	if (ri->data.mouse.usButtonFlags) {
		g_kbm_active.store(true, std::memory_order_relaxed);
	}
}

void win32_input_dispatch(UINT msg, WPARAM wParam, LPARAM lParam, HWND hwnd) {
	switch (msg) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		if (wParam < 256) {
			AcquireSRWLockExclusive(&g_input_lock);
			if (!g_keyboard.key_down[wParam]) {
				g_keyboard.key_down[wParam] = true;
				g_keys_held.fetch_add(1, std::memory_order_relaxed);
			}
			ReleaseSRWLockExclusive(&g_input_lock);
			g_kbm_active.store(true, std::memory_order_relaxed);
		}
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP: {
		if (wParam < 256) {
			AcquireSRWLockExclusive(&g_input_lock);
			if (g_keyboard.key_down[wParam]) {
				g_keyboard.key_down[wParam] = false;
				g_keys_held.fetch_sub(1, std::memory_order_relaxed);
			}
			ReleaseSRWLockExclusive(&g_input_lock);
		}
		break;
	}
	case WM_MOUSEMOVE: {
		// Track absolute position for menu hit-testing — relative motion
		// comes from WM_INPUT to avoid SetCursorPos feedback loops. Also
		// keep the virtual cursor in sync with the real one so toggling
		// cursor capture doesn't teleport the in-game pointer.
		const int x = GET_X_LPARAM(lParam);
		const int y = GET_Y_LPARAM(lParam);
		// Suppress the WM_MOUSEMOVE that our own SetCursorPos generates
		// while cursor capture is active (the raw-input handler drags the
		// OS cursor along with the virtual cursor). Without this guard the
		// echoed message would simply restate the position we just set —
		// harmless but noisy — and any latency between SetCursorPos and
		// the echo could overwrite a newer raw-input update.
		if (g_cursor_captured.load(std::memory_order_acquire)) {
			POINT screen{ x, y };
			if (hwnd) ClientToScreen(hwnd, &screen);
			const int sx = g_synth_cursor_x.load(std::memory_order_acquire);
			const int sy = g_synth_cursor_y.load(std::memory_order_acquire);
			if (sx == screen.x && sy == screen.y) {
				// Echo of our own SetCursorPos — refresh client size only.
				RECT rc{};
				if (hwnd && GetClientRect(hwnd, &rc)) {
					AcquireSRWLockExclusive(&g_input_lock);
					g_client_w = rc.right  - rc.left;
					g_client_h = rc.bottom - rc.top;
					ReleaseSRWLockExclusive(&g_input_lock);
				}
				break;
			}
		}
		// Refresh client size cheaply each move; covers resize/F11 fullscreen.
		RECT rc{};
		if (hwnd && GetClientRect(hwnd, &rc)) {
			AcquireSRWLockExclusive(&g_input_lock);
			g_client_w = rc.right  - rc.left;
			g_client_h = rc.bottom - rc.top;
			g_mouse_pX = x;
			g_mouse_pY = y;
			g_virt_cursor_x = (float)x;
			g_virt_cursor_y = (float)y;
			ReleaseSRWLockExclusive(&g_input_lock);
		} else {
			AcquireSRWLockExclusive(&g_input_lock);
			g_mouse_pX = x;
			g_mouse_pY = y;
			g_virt_cursor_x = (float)x;
			g_virt_cursor_y = (float)y;
			ReleaseSRWLockExclusive(&g_input_lock);
		}
		break;
	}
	case WM_INPUT: {
		handle_raw_input(lParam);
		break;
	}
	case WM_MOUSEWHEEL: {
		const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
		AcquireSRWLockExclusive(&g_input_lock);
		g_mouse_lZ += (float)delta / WHEEL_DELTA;
		ReleaseSRWLockExclusive(&g_input_lock);
		break;
	}
	case WM_LBUTTONDOWN: {
		set_button(0, true);
		g_kbm_active.store(true, std::memory_order_relaxed);
		// Diagnostic: log first few + every 32nd LMB press so we can verify
		// the OS is delivering WM_LBUTTONDOWN to halox's wndproc (vs raw input
		// intercepting silently). If LMB press never logs here, OS is routing
		// the click somewhere else.
		static std::atomic<int> s_lmb_log{0};
		int n = s_lmb_log.fetch_add(1, std::memory_order_relaxed) + 1;
		if (n <= 4 || (n % 32) == 0) {
			CONSOLE_LOG_INFO("input: WM_LBUTTONDOWN #%d (kbm sticky→true)", n);
		}
		break;
	}
	case WM_LBUTTONUP:   set_button(0, false); break;
	case WM_RBUTTONDOWN: set_button(1, true);  g_kbm_active.store(true, std::memory_order_relaxed); break;
	case WM_RBUTTONUP:   set_button(1, false); break;
	case WM_MBUTTONDOWN: set_button(2, true);  g_kbm_active.store(true, std::memory_order_relaxed); break;
	case WM_MBUTTONUP:   set_button(2, false); break;
	case WM_XBUTTONDOWN: set_button(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4, true); g_kbm_active.store(true, std::memory_order_relaxed); break;
	case WM_XBUTTONUP:   set_button(GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4, false); break;
	case WM_KILLFOCUS: {
		// Drop all key/button state when the window loses focus so keys
		// don't appear stuck after Alt-Tab.
		AcquireSRWLockExclusive(&g_input_lock);
		memset(&g_keyboard, 0, sizeof(g_keyboard));
		g_keys_held.store(0, std::memory_order_relaxed);
		g_mouse_buttons = 0;
		g_mouse_lX = g_mouse_lY = g_mouse_lZ = 0.0f;
		ReleaseSRWLockExclusive(&g_input_lock);
		break;
	}
	default: break;
	}
}

// Raw mouse counts (WM_INPUT) tend to be far larger per inch than Halo's
// expected DirectInput relative counts, so we scale down hard. The
// sensitivity multiplier on top lets users tune from the launcher.
static constexpr float k_mouse_delta_scale = 0.10f;

static std::atomic<float> g_mouse_sensitivity{ 0.10f };
static std::atomic<bool>  g_invert_y{ false };

float win32_input_get_mouse_sensitivity() {
	return g_mouse_sensitivity.load(std::memory_order_relaxed);
}
void win32_input_set_mouse_sensitivity(float v) {
	if (v < 0.0f) v = 0.0f;
	if (v > 50.0f) v = 50.0f;
	g_mouse_sensitivity.store(v, std::memory_order_relaxed);
}
bool win32_input_get_invert_y() {
	return g_invert_y.load(std::memory_order_relaxed);
}
void win32_input_set_invert_y(bool v) {
	g_invert_y.store(v, std::memory_order_relaxed);
}

void win32_input_set_cursor_capture(HWND hwnd, bool captured) {
	g_capture_hwnd.store(hwnd, std::memory_order_release);
	g_cursor_captured.store(captured && hwnd != nullptr, std::memory_order_release);
	if (captured && hwnd) {
		// Seed the OS cursor at the virtual cursor's current screen
		// position so there's no jump on the first raw-input event after
		// capture. Without this, the OS cursor sits wherever it was when
		// capture began (e.g. the user's click point) and the next mouse
		// move would teleport it to virt_x/virt_y.
		float vx, vy;
		AcquireSRWLockShared(&g_input_lock);
		vx = g_virt_cursor_x;
		vy = g_virt_cursor_y;
		ReleaseSRWLockShared(&g_input_lock);
		POINT pt{ (LONG)vx, (LONG)vy };
		if (ClientToScreen(hwnd, &pt)) {
			g_synth_cursor_x.store(pt.x, std::memory_order_release);
			g_synth_cursor_y.store(pt.y, std::memory_order_release);
			SetCursorPos(pt.x, pt.y);
		}
	} else {
		g_synth_cursor_x.store(INT_MIN, std::memory_order_release);
		g_synth_cursor_y.store(INT_MIN, std::memory_order_release);
	}
}

void win32_input_snapshot(s_keyboard_state* keyboard, s_mouse_state* mouse) {
	AcquireSRWLockExclusive(&g_input_lock);
	if (keyboard) *keyboard = g_keyboard;
	if (mouse) {
		const float sens   = g_mouse_sensitivity.load(std::memory_order_relaxed) * k_mouse_delta_scale;
		const float y_sign = g_invert_y.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
		mouse->lX        = g_mouse_lX * sens;
		mouse->lY        = g_mouse_lY * sens * y_sign;
		// Emit the virtual cursor for absolute position so menu hit-tests
		// work even when the OS cursor is hidden + clipped (in which case
		// WM_MOUSEMOVE stops firing and g_mouse_pX/pY would freeze). Falls
		// back to the WM_MOUSEMOVE-tracked value when no raw deltas have
		// arrived yet — the WM_MOUSEMOVE handler keeps both in sync.
		mouse->pX        = g_virt_cursor_x;
		mouse->pY        = g_virt_cursor_y;
		mouse->lZ        = g_mouse_lZ;
		mouse->lZScale   = 1.0f;
		mouse->buttons   = g_mouse_buttons;
	}
	// Consume relative deltas — the next snapshot reports only fresh motion.
	g_mouse_lX = g_mouse_lY = g_mouse_lZ = 0.0f;
	ReleaseSRWLockExclusive(&g_input_lock);
}

bool win32_input_keyboard_active() {
	return g_keys_held.load(std::memory_order_relaxed) > 0;
}

int win32_input_peek_held(uint8_t kb_out[256]) {
	int buttons;
	AcquireSRWLockShared(&g_input_lock);
	if (kb_out) {
		for (int i = 0; i < 256; ++i) {
			kb_out[i] = g_keyboard.key_down[i] ? 1 : 0;
		}
	}
	buttons = g_mouse_buttons;
	ReleaseSRWLockShared(&g_input_lock);
	return buttons;
}

bool win32_input_kbm_sticky_active() {
	return g_kbm_active.load(std::memory_order_relaxed);
}

void win32_input_clear_kbm_sticky() {
	g_kbm_active.store(false, std::memory_order_relaxed);
}

void win32_input_drain_deltas() {
	AcquireSRWLockExclusive(&g_input_lock);
	g_mouse_lX = g_mouse_lY = g_mouse_lZ = 0.0f;
	ReleaseSRWLockExclusive(&g_input_lock);
}
