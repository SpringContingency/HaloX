#include "game_instance_manager.h"

#include "../main/main.h"
#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"

using namespace libmcc;

LRESULT c_game_instance_manager::process_message(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam
) {
	if (m_game_thread && g_win32_parameter.cursor_capture) {
		SetCursorPos(
			g_win32_parameter.cursor_position.x,
			g_win32_parameter.cursor_position.y
		);
	}

	switch (uMsg) {
	case WM_KEYDOWN: {
		switch (wParam) {
		case VK_ESCAPE: {
			if (!m_game_thread) {
				return 0;
			}

			auto msg = m_game_paused ?
				_game_message_resume:
				_game_message_pause;

			m_game_engine->post_message(msg, nullptr);

			m_game_paused = !m_game_paused;

			return 0;
		}
		case VK_F3: {
			if (!m_game_thread) {
				return 0;
			}

			auto& cursor_capture = g_win32_parameter.cursor_capture;
			auto& cursor_position = g_win32_parameter.cursor_position;

			cursor_capture = !cursor_capture;
			if (cursor_capture) {
				RECT rect;
				int counter;

				GetWindowRect(hWnd, &rect);
				cursor_position.x = rect.left + (rect.right - rect.left) / 2;
				cursor_position.y = rect.top + (rect.bottom - rect.top) / 2;

				do {counter = ShowCursor(false);} while (counter >= 0);
			} else {
				int counter_old, counter_new;

				counter_old = ShowCursor(true);

				while (counter_old < 0) {
					counter_new = ShowCursor(true);

					if (counter_new == counter_old) {
						break; // mouse is not installed
					}

					counter_old = counter_new;
				}
			}

			auto game_focused = !g_game_focused;

			if (!game_focused) {
				g_message_queue.flush(nullptr);
			}

			InterlockedExchange(&g_game_focused, game_focused);

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
		default:
			break;
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
		launch_game_internal();
		return 1;
	}
	case _window_message_game_restart:
	case _window_message_game_quit:
	case WM_CLOSE: {
		if (!m_game_thread) {
			return 0;
		}

		if (uMsg == _window_message_game_restart) {
			auto reason = static_cast<e_game_restart_reason>(wParam);
			auto message = reinterpret_cast<const char*>(lParam);

			CONSOLE_LOG_DEBUG("GAME QUIT[%d]: %s", reason, message);
		} else {
			// todo: delay execution if game is not running
			m_game_engine->post_message(_game_message_quit, nullptr);
			m_game_engine->post_message(_game_message_resume, nullptr);
		}

		while (
			!GetExitCodeThread(m_game_thread, &m_game_thread_exit_code) ||
			m_game_thread_exit_code == STILL_ACTIVE
		) {
			SwitchToThread();
		}

		m_game_thread = nullptr;
		m_game_paused = false;

		if (m_game_engine) {
			m_game_engine->free();
			m_game_engine = nullptr;
		}

		return 0;
	}
	case WM_ACTIVATEAPP: {
		if (!m_game_thread) {
			return 0;
		}

		if (wParam) {
			m_game_engine->post_message(_game_message_resume, nullptr);
		} else {
			m_game_engine->post_message(_game_message_pause, nullptr);
		}

		return 0;
	}
	default:
		break;
	}

	return 0;
}
