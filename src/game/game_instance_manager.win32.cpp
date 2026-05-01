#include "game_instance_manager.h"

#include "../main/main.h"
#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"
#include "../input/win32_input.h"
#include "../ui/ui_launch.h"
#include "../ui/ui_progress.h"
#include "halo2_native_overrides.h"

// Trampoline so ui_launch.cpp can drive launch_game_internal without exposing
// the private member or pulling the manager header into the ui module.
namespace halox::ui {
int ui_launch_run_internal() {
	return ::game_instance_manager()->launch_game_internal_for_worker();
}
}  // namespace halox::ui

using namespace libmcc;

// Borderless-fullscreen toggle (F11). Saves the windowed style/rect on the
// first call so toggling back restores exactly what the user had before.
struct s_fullscreen_state {
	bool      active = false;
	LONG_PTR  saved_style = 0;
	LONG_PTR  saved_ex_style = 0;
	RECT      saved_rect{};
	bool      saved_rect_valid = false;
};
static s_fullscreen_state g_fullscreen;

static void toggle_borderless_fullscreen(HWND hWnd) {
	if (!g_fullscreen.active) {
		// Save current windowed state.
		g_fullscreen.saved_style    = GetWindowLongPtrW(hWnd, GWL_STYLE);
		g_fullscreen.saved_ex_style = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
		if (GetWindowRect(hWnd, &g_fullscreen.saved_rect))
			g_fullscreen.saved_rect_valid = true;

		// Strip the frame and stretch to the monitor that owns this window.
		HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		GetMonitorInfoW(monitor, &mi);

		LONG_PTR new_style = (g_fullscreen.saved_style
			& ~(WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU))
			| WS_POPUP;
		SetWindowLongPtrW(hWnd, GWL_STYLE, new_style);
		SetWindowLongPtrW(hWnd, GWL_EXSTYLE,
			g_fullscreen.saved_ex_style & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

		SetWindowPos(hWnd, HWND_TOP,
			mi.rcMonitor.left, mi.rcMonitor.top,
			mi.rcMonitor.right  - mi.rcMonitor.left,
			mi.rcMonitor.bottom - mi.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

		g_fullscreen.active = true;
	} else {
		// Restore windowed state.
		SetWindowLongPtrW(hWnd, GWL_STYLE,    g_fullscreen.saved_style);
		SetWindowLongPtrW(hWnd, GWL_EXSTYLE,  g_fullscreen.saved_ex_style);

		if (g_fullscreen.saved_rect_valid) {
			const RECT& r = g_fullscreen.saved_rect;
			SetWindowPos(hWnd, nullptr,
				r.left, r.top, r.right - r.left, r.bottom - r.top,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		} else {
			SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		}

		g_fullscreen.active = false;
	}
}

// Forward decl so dismiss_pause_overlay() can re-grab the cursor without a
// circular include — apply_cursor_capture is local to this TU.
static void apply_cursor_capture(HWND hWnd, bool capture);

void c_game_instance_manager::dismiss_pause_overlay() {
	// Hide overlay first so the next imgui frame doesn't render the menu.
	InterlockedExchange(&g_show_imgui, FALSE);
	// Resume the engine. SEH-wrap because some game DLLs misbehave on the
	// pause/resume path (older-ABI titles); a failed resume should warn but
	// not crash halox.
	if (m_game_engine && m_game_paused) {
		__try {
			m_game_engine->post_message(_game_message_resume, nullptr);
			m_game_paused = false;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			CONSOLE_LOG_WARN("dismiss_pause_overlay: post_message(resume) raised SEH 0x%08lX",
				GetExceptionCode());
		}
	}
	// Drain raw-mouse delta accumulated while paused so the engine doesn't
	// see one giant mouse move on its next tick (camera-snap fix).
	win32_input_drain_deltas();
	// Re-grab the cursor for gameplay.
	HWND hwnd = g_win32_parameter.window_handle;
	if (hwnd) apply_cursor_capture(hwnd, true);
}

static void apply_cursor_capture(HWND hWnd, bool capture) {
	auto& cursor_capture = g_win32_parameter.cursor_capture;

	if (capture == cursor_capture) return;
	cursor_capture = capture;

	if (capture) {
		// Hide cursor and clip it to the client area so it can't escape.
		// Raw input (WM_INPUT) is what feeds the game's mouse-look — the
		// physical cursor position no longer matters for input.
		int counter;
		do { counter = ShowCursor(FALSE); } while (counter >= 0);

		RECT client; GetClientRect(hWnd, &client);
		POINT tl{ client.left, client.top };
		POINT br{ client.right, client.bottom };
		ClientToScreen(hWnd, &tl);
		ClientToScreen(hWnd, &br);
		RECT clip{ tl.x, tl.y, br.x, br.y };
		ClipCursor(&clip);
		// Tell the input layer to drag the OS cursor along with the virtual
		// cursor while capture is active. Game UIs that read GetCursorPos
		// directly (Reach pause/loadout menu) need a moving OS cursor —
		// our s_mouse_state::pX/pY virtual-cursor pipe doesn't reach them.
		win32_input_set_cursor_capture(hWnd, true);
	} else {
		win32_input_set_cursor_capture(hWnd, false);
		ClipCursor(nullptr);
		int counter_old = ShowCursor(TRUE);
		while (counter_old < 0) {
			int counter_new = ShowCursor(TRUE);
			if (counter_new == counter_old) break;  // no mouse attached
			counter_old = counter_new;
		}
	}

	auto game_focused = capture ? TRUE : FALSE;
	if (!game_focused) g_message_queue.flush(nullptr);
	InterlockedExchange(&g_game_focused, game_focused);
}

LRESULT c_game_instance_manager::process_message(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
) {
	// (Old SetCursorPos-recenter loop removed — it caused phantom WM_MOUSEMOVE
	// feedback that drifted the camera. Cursor is now hidden + clipped via
	// ClipCursor in apply_cursor_capture; deltas come from WM_INPUT.)

	switch (uMsg) {
	case WM_KEYDOWN: {
		switch (wParam) {
		case VK_ESCAPE: {
			if (!m_game_thread) {
				return 0;
			}

			// Suppress ESC while the game is still loading — toggling the
			// overlay during the load phase causes the pause panel to flash
			// over the loading screen. The loading screen owns the screen
			// until liveness reports stable alive frames.
			if (halox::ui::g_launch_in_progress ||
			    halox::ui::progress_is_loading()) {
				return 0;
			}

			LONG show = !g_show_imgui;
			if (show) {
				// Pump any queued window messages into ImGui so the overlay
				// can react to mouse clicks immediately on the first frame.
				g_message_queue.flush(nullptr);
			}
			InterlockedExchange(&g_show_imgui, show);
			// Release the cursor when bringing the overlay up so the user can
			// click; re-capture when dismissing.
			apply_cursor_capture(hWnd, !show);

			// Actually freeze the game simulation. WAS stripped because halo2
			// AV'd here in older builds, but the user wants pause-on-ESC
			// without auto-pause on alt-tab (WM_ACTIVATEAPP no longer pauses).
			// SEH-wrap the post in case the engine still misbehaves on a
			// specific module — a failed pause shouldn't kill the program.
			if (m_game_engine) {
				auto msg = show ? _game_message_pause : _game_message_resume;
				__try {
					m_game_engine->post_message(msg, nullptr);
					m_game_paused = show;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					CONSOLE_LOG_WARN("ESC: post_message(%s) raised SEH 0x%08lX — overlay shown but engine not frozen",
						show ? "pause" : "resume", GetExceptionCode());
				}
			}
			// Drain accumulated raw-mouse delta. While the engine is paused
			// it stops calling get_input_state, so g_mouse_lX/lY collect
			// every motion the user made navigating the overlay. Without
			// this drain, the very first tick after resume hands the game
			// the entire pause-time delta as one frame's mouse motion —
			// the camera "shoots" to wherever the cursor went.
			win32_input_drain_deltas();
			return 0;
		}
		case VK_F3: {
			if (!m_game_thread) return 0;
			apply_cursor_capture(hWnd, !g_win32_parameter.cursor_capture);
			return 0;
		}
		case VK_F4: {
			if (!m_game_thread) {
				return 0;
			}

			LONG show_imgui = !g_show_imgui;

			if (show_imgui) {
				g_message_queue.flush(nullptr);
			}

			InterlockedExchange(&g_show_imgui, show_imgui);

			break;
		}
		case VK_F11: {
			// Borderless-fullscreen toggle. Pair with F4 (hide imgui) for a
			// pure game-only view; F11 again returns to a windowed layout.
			toggle_borderless_fullscreen(hWnd);
			// Re-clip the cursor to the new client rect if we were captured.
			if (g_win32_parameter.cursor_capture) {
				g_win32_parameter.cursor_capture = false;  // force re-apply
				apply_cursor_capture(hWnd, true);
			}
			return 0;
		}
		default:
			break;
		}
		break;
	}
	case WM_LBUTTONDOWN: {
		// Clicking the window grabs the cursor when a game is running and
		// the imgui menus aren't on top. F3 still toggles back out for menus.
		if (m_game_thread && !g_show_imgui && !g_win32_parameter.cursor_capture) {
			apply_cursor_capture(hWnd, true);
		}
		break;
	}
	case _window_message_game_resize: {
		rasterizer()->resize(
			g_win32_parameter.window_width,
			g_win32_parameter.window_height);
		return 0;
	}
	case _window_message_game_launch: {
		// Off-thread launch — the heavy game DLL init steps (set_library_settings
		// → create_game_engine → initialize → initialize_game) used to run on
		// this message-pump thread, which froze the entire halox window for the
		// duration of the launch (and forever if a step hung). ui_launch_kick
		// spawns a worker thread, hides the imgui chrome, and lets the main
		// loop keep pumping messages while the launch runs.
		halox::ui::ui_launch_kick();
		return 1;
	}
	case _window_message_game_launch_finished: {
		// Posted by the main loop once the worker thread reports a completed
		// launch and a game thread is alive. We do the cursor-capture here
		// (on the message-pump thread) so input ownership is established by
		// the same thread that owns the window.
		if (m_game_thread && !g_show_imgui) {
			apply_cursor_capture(hWnd, true);
		}
		return 1;
	}
	case _window_message_game_restart:
	case _window_message_game_quit:
	case WM_CLOSE: {
		if (!m_game_thread) {
			return 0;
		}

		// Arm the quit-cleanup window. While the counter is non-zero, mss64.dll
		// AVs (Miles Sound System worker thread tripping over torn-down halo2
		// state) are redirected to ExitThread instead of killing the launcher.
		// Stays armed through the silence_audio + TerminateThread + module
		// cycle sequence, plus an extra moment afterwards for in-flight worker
		// callbacks to drain. Raw Interlocked* (not the RAII guard) because
		// this function uses __try, and MSVC forbids C++ object unwinding in
		// SEH functions (C2712).
		InterlockedIncrement(&g_quit_cleanup_active);

		// Two flavors of game-end:
		//   1. ENGINE-INITIATED (_window_message_game_restart): engine called
		//      restart_game() because the round/match/mission ended or the
		//      user hit the in-game quit button and the engine is now
		//      running its outro (fade-out, credits, "you died" screen,
		//      end-of-mission cinematic). The engine knows what it's doing;
		//      we shouldn't post quit (which short-circuits the outro) and
		//      shouldn't silence audio. We wait long enough for the outro
		//      to finish naturally.
		//   2. USER-INITIATED (WM_CLOSE / _window_message_game_quit): halox
		//      window closed or the launcher invoked quit directly. Engine
		//      doesn't know yet; post quit to it, silence audio, fast wait.
		const bool engine_initiated = (uMsg == _window_message_game_restart);
		const DWORD wait_ms = engine_initiated ? 15000 : 2000;

		if (engine_initiated) {
			auto reason = static_cast<e_game_restart_reason>(wParam);
			auto message = reinterpret_cast<const char*>(lParam);
			CONSOLE_LOG_DEBUG("GAME QUIT[%d]: %s", reason, message);
		} else {
			// Send the engine's quit/resume messages, BUT defensively — older
			// ABI games (halo2) AV inside their pause/resume path. SEH-wrap so
			// a bad call doesn't take the whole launcher down.
			__try {
				m_game_engine->post_message(_game_message_quit, nullptr);
				m_game_engine->post_message(_game_message_resume, nullptr);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				CONSOLE_LOG_WARN("game_quit: post_message raised SEH 0x%08lX (proceeding to thread teardown)",
					GetExceptionCode());
			}

			// Halo2 keeps audio threads ticking after we kill the game thread —
			// silence them BEFORE the wait so the menu doesn't get a tail of
			// in-game music/sfx. Skipped on engine-initiated paths so the
			// outro audio plays. No-op for other modules.
			__try {
				halo2_silence_audio();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				CONSOLE_LOG_WARN("game_quit: halo2_silence_audio raised SEH 0x%08lX",
					GetExceptionCode());
			}
		}

		// Bounded wait. Long for engine-initiated outros (so we don't kill
		// the engine mid-cinematic and snap the user to the launcher);
		// short for user-quit (snappy response).
		const DWORD wait_rc = WaitForSingleObject(m_game_thread, wait_ms);
		if (wait_rc != WAIT_OBJECT_0) {
			CONSOLE_LOG_WARN("game_quit: game thread didn't exit within %lums "
				"(rc=0x%lX) — forcing TerminateThread", wait_ms, wait_rc);
			TerminateThread(m_game_thread, 0);
			// Give Windows a moment to actually clean up the thread.
			WaitForSingleObject(m_game_thread, 500);
		}
		GetExitCodeThread(m_game_thread, &m_game_thread_exit_code);
		CloseHandle(m_game_thread);

		m_game_thread = nullptr;
		m_game_paused = false;

		if (m_game_engine) {
			__try {
				m_game_engine->free();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				CONSOLE_LOG_WARN("game_quit: m_game_engine->free raised SEH 0x%08lX",
					GetExceptionCode());
			}
			m_game_engine = nullptr;
		}

		// Per-game pre-unload cleanup. Hooks installed by halox into the game
		// DLL must be removed BEFORE FreeLibrary unmaps the target bytes —
		// otherwise MH_RemoveHook on the next launch would touch freed memory
		// (or the next install bails on stale g_hooked_module bookkeeping).
		if (m_game == _module_halo2) {
			__try {
				halo2_uninstall_input_hooks();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				CONSOLE_LOG_WARN("game_quit: halo2_uninstall_input_hooks raised SEH 0x%08lX",
					GetExceptionCode());
			}
		}

		// Cycle the game's DLL: unload + reload. m_game_engine->free()
		// releases the engine object but leaves the DLL's static state
		// (texture/level pools, cached scenarios, hook detours) in memory,
		// so a subsequent launch of a DIFFERENT game inherits stale state
		// and a re-launch of the SAME game can fail mid-init. Cycling
		// resets the DLL to fresh-from-disk state. Wrapped in SEH because
		// some DLLs misbehave on FreeLibrary (TLS callbacks, atexit chains).
		if (m_game >= 0 && m_game < k_game_count) {
			s_module_flags one;
			one.bit_set(m_game, true);
			__try {
				unload_modules(one);
				load_modules(one);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				CONSOLE_LOG_WARN("game_quit: module cycle for game=%d raised SEH 0x%08lX",
					(int)m_game, GetExceptionCode());
			}
		}

		// Make sure the launcher imgui is visible after a quit so the user
		// has something to interact with.
		InterlockedExchange(&g_show_imgui, TRUE);
		// Release any cursor capture so the user can click the launcher.
		apply_cursor_capture(hWnd, false);

		// Disarm the quit-cleanup window. Pair with the InterlockedIncrement
		// at the top of this case. After this point, mss64.dll AVs once again
		// kill the process (preserving real-bug visibility outside quit).
		InterlockedDecrement(&g_quit_cleanup_active);

		return 0;
	}
	case WM_ACTIVATEAPP: {
		if (!m_game_thread) {
			return 0;
		}

		if (wParam) {
			// Re-grab the cursor on focus return; otherwise Alt-Tab back leaves
			// the cursor visible until the user clicks again.
			if (!g_show_imgui) apply_cursor_capture(hWnd, true);
		} else {
			// Do NOT pause the engine on focus loss — multi-instance MP needs
			// both processes ticking simultaneously, and only one can hold
			// foreground focus. Just release the cursor.
			apply_cursor_capture(hWnd, false);
		}

		return 0;
	}
	case WM_SETFOCUS: {
		// Plain window-focus (e.g. clicking on the title bar) should also
		// re-capture the cursor when a game is running and the menu is hidden.
		if (m_game_thread && !g_show_imgui) {
			apply_cursor_capture(hWnd, true);
		}
		break;
	}
	case WM_KILLFOCUS: {
		apply_cursor_capture(hWnd, false);
		break;
	}
	default:
		break;
	}

	return 0;
}
