#include "main.h"

#include "../text/font_cache.h"
#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"
#include "../player/player_manager.h"
#include "../game/game_instance_manager.h"

#include "../render/imgui_main_view.h"
#include "../render/imgui_game_view.h"
#include "../input/win32_input.h"
#include "../game/mcc_user_settings.h"
#include "../diag/launch_liveness.h"
#include "../diag/thread_trace.h"
#include "../network/mcc_network_bridge.h"
#include "../network/instance_discovery.h"
#include "../network/instance_isolation.h"

#include "../ui/ui_theme.h"
#include "../ui/ui_launch.h"

#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

using namespace libmcc;

LRESULT window_process_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LONG g_show_imgui_cached = true;
volatile LONG g_show_imgui = true;
volatile LONG g_game_focused = false;
c_fixed_thread_safe_queue<MSG> g_message_queue;
s_win32_parameter g_win32_parameter {
	.window_proc = window_process_message,
	.window_width = 1280,
	.window_height = 720,
	.class_name = L"main_window_class",
	.window_name = TOSTRINGW(PROJECT_NAME),
};

static bool request_resize = false;

const wchar_t* main_get_root_folder() {
	static std::wstring root_folder;

	if (root_folder.empty()) {
		auto document_folder = win32_get_document_folder_path();
		std::vector<wchar_t> buffer(document_folder.size() + MAX_PATH);
		swprintf_s(
			buffer.data(),
			buffer.size(),
			L"%s\\%s",
			document_folder.c_str(),
			L"halox"
		);

		root_folder = buffer.data();
	}
	return root_folder.c_str();
}

int APIENTRY WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nShowCmd
) {
	MSG msg;
	bool exit;
	int status;
	int main_thread_id;

	exit = false;

	g_win32_parameter.cmd_show = nShowCmd;
	g_win32_parameter.cmd_line = lpCmdLine;
	g_win32_parameter.instance_handle = hInstance;
	g_win32_parameter.window_thread = GetCurrentThreadId();

	SetDllDirectoryW(L"./mcc/binaries/win64");

	// Allow per-directory DLL search. Required so AddDllDirectory(...) calls
	// performed later (per-game folders, MCC root) participate in the loader's
	// search order, including for delay-loaded imports inside the game DLLs.
	if (!SetDefaultDllDirectories(
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS)) {
		// Extremely rare: continue anyway. The launcher remains functional;
		// only delay-load resolution from non-default dirs would be impacted.
	}

	console_logger()->initialize();
	// Parse MCC's GameUserSettings.ini early so we can apply the user's
	// preferred resolution + window mode to the swap chain BEFORE the
	// rasterizer creates the window. The full mcc_user_settings_initialize()
	// runs again later (post-game-manager) for the per-game profile-stamp
	// path; this early call is just the same parser, which is idempotent.
	mcc_user_settings_initialize();
	// NOTE: deliberately do NOT apply graphics.resolution_x/y to the launcher
	// window — those are the user's preferred IN-GAME render resolution, not
	// the launcher chrome size.  Forcing the launcher to MCC's native res
	// breaks the chrome on high-DPI displays (UI is microscopic at 4K) and,
	// combined with fullscreen_mode, made the window non-draggable.  The
	// launcher always opens at its 1280x720 default; the in-game swap-chain
	// resolution is a separate concern (handled by rasterizer.x3d11.cpp).
	// Per-instance kernel-object namespace. Two halox processes loading the
	// same haloreach.dll would otherwise collide on every named mutex/event/
	// file-mapping the engine creates for singleton coordination. Must run
	// BEFORE any code that might create or open named kernel objects (i.e.
	// before MCC/game DLLs are touched).
	halox::network::instance_isolation_install();
	// Hook CreateThread so any thread spawned later (notably the game thread
	// that initialize_game creates inside the game DLL) gets its parent's
	// stack snapshotted. When that thread crashes, the unhandled-exception
	// filter prints "creator stack" alongside the call stack — useful since
	// child threads bottom out at BaseThreadInitThunk with no other context.
	// Must run before any module gets a chance to spawn threads.
	thread_trace_install();
	rasterizer()->initialize();
	// Halo classic-modern hybrid theme — applies global ImGuiStyle colors and
	// sizing so every imgui surface inherits the same look. Must run after
	// the rasterizer (which creates the ImGui context) and before any view
	// renders for the first time.
	halox::ui::ui_theme_apply();
	font_cache()->initialize();
	mcc_user_settings_initialize();
	player_manager()->initialize();
	game_instance_manager()->initialize();

	// Load MCC's network stack DLLs and resolve send/recv trampolines as a
	// research scaffold — does not yet route any game-DLL packets through
	// MCC. Must run after game_instance_manager()->initialize() so that cwd
	// is the MCC content root and AddDllDirectory'd folders are registered.
	// Idempotent; logs status. See src/network/mcc_network_bridge.h for
	// current limitations (datafile mapping ⇒ resolved pointers are not
	// safe to invoke yet).
	halox::network::mcc_bridge_init();

	// File-based discovery service so other halox processes can find this one
	// and offer to join. Heartbeat lives at %TEMP%\halox-instances\<uuid>.halox
	// and gets refreshed every 2s. See src/network/instance_discovery.h.
	halox::network::instance_discovery_init();

	// Raw mouse input — needs the rasterizer's window to exist first.
	if (g_win32_parameter.window_handle) {
		win32_input_register_raw_mouse(g_win32_parameter.window_handle);
	}

	// Parse `--launch=<module>:<mode>:<map>[:<difficulty>[:<variant>]]` from
	// the command line. Queued here; fired below once we've rendered the
	// first frame so the rasterizer / game manager are fully warm.
	if (lpCmdLine && *lpCmdLine) {
		const char* k = "--launch=";
		const char* hit = std::strstr(lpCmdLine, k);
		if (hit) game_view_request_launch_from_spec(hit + std::strlen(k));
	}

	while (!exit) {
		while (true) {
			status = PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE);

			if (status == 0) {
				break;
			}

			if (msg.message == WM_QUIT) {
				exit = true;
				break;
			}
			
			TranslateMessage(&msg);
			DispatchMessageW(&msg);

			// Always feed window messages to the input capture so keyboard/mouse
			// state flows to halo3.dll once a game is running. ImGui still owns
			// the messages while not in-game so the launcher menu is usable.
			win32_input_dispatch(msg.message, msg.wParam, msg.lParam, msg.hwnd);

			if (game_instance_manager()->in_game()) {
				g_message_queue.push(&msg);
			} else {
				ImGui_ImplWin32_WndProcHandler(
					msg.hwnd,
					msg.message,
					msg.wParam,
					msg.lParam);
			}
		}

		if (request_resize) {
			request_resize = false;

			if (game_instance_manager()->in_game()) {
				game_instance_manager()->post_message(_game_message_resize);
			} else {
				rasterizer()->resize(
					g_win32_parameter.window_width,
					g_win32_parameter.window_height);
			}
		}

		if (game_instance_manager()->in_game()) {
			// On the FIRST tick after a successful launch, post the
			// "launch finished" message so the win32 handler can apply
			// cursor capture on the message-pump thread.
			static bool s_finish_posted = false;
			if (!s_finish_posted) {
				s_finish_posted = true;
				PostMessageW(g_win32_parameter.window_handle,
					_window_message_game_launch_finished, 0, 0);
			}
			// liveness_probe_tick is now driven from c_game_manager::end_frame
			// (game thread). Calling it here from the UI thread races the
			// game thread's d3d11 immediate-context use and AVs inside
			// d3d11.dll!CreateDirect3D11SurfaceFromDXGISurface.
			//
			// SwitchToThread() is a no-op when no other thread on this
			// logical processor is ready, so the message pump busy-spins at
			// 100% CPU and contends with the game thread for cache/cores —
			// causing perceptible lag on heavy renderers (halo2 Anniversary).
			// MsgWaitForMultipleObjectsEx blocks for up to 1ms or until a
			// Windows message arrives, freeing the core for the game thread
			// without delaying input dispatch.
			MsgWaitForMultipleObjectsEx(0, nullptr, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			continue;
		}

		// Update the window title to reflect launch progress (cheap, no D3D
		// involvement). The animated loading screen below picks up the same
		// status string and renders it big.
		static char s_last_title[256] = {};
		char status[200] = {};
		halox::ui::ui_launch_get_status(status, sizeof(status));
		char title[256];
		if (halox::ui::g_launch_in_progress && status[0]) {
			std::snprintf(title, sizeof(title), "halox — %s", status);
		} else {
			std::snprintf(title, sizeof(title), "halox");
		}
		if (std::strcmp(title, s_last_title) != 0) {
			std::strncpy(s_last_title, title, sizeof(s_last_title) - 1);
			SetWindowTextA(g_win32_parameter.window_handle, title);
		}

		// Drain finished-launch state (re-shows imgui on failure).
		halox::ui::ui_launch_pump();

		// D3D immediate-context contention guard: while the launch worker is
		// driving a game DLL's device init (PreloadCommonBegin / RT vtable
		// swap / engine init), the UI thread MUST NOT submit d3d11 commands.
		// halo1 in particular crashes inside d3d11.dll at rva=0x1155E4 when
		// the UI thread's begin/end_frame races the worker's device calls
		// (project_halox_d3d11_contention class). Skip the rasterizer block
		// while launch is in progress — the screen freezes on the last
		// rendered frame for the few seconds of init, then the in_game
		// branch above takes over (game thread owns the context). Window
		// title set above still updates so the user sees launch progress.
		if (InterlockedCompareExchange(&halox::ui::g_launch_in_progress, 0, 0) != 0) {
			SwitchToThread();
			continue;
		}

		rasterizer()->begin_frame();
		// Shell renders the entire menu — left rail nav, content pane, right
		// rail (sessions), top + bottom strips. The old free-floating
		// c_imgui_game_mainmenu_view window is no longer rendered here; its
		// body is embedded inline in the shell's GAME page.
		c_imgui_main_view().render();
		rasterizer()->end_frame();

		// Consume any pending --launch request after the first full frame so
		// the launcher UI / rasterizer / game_instance_manager are all live.
		// Fires at most once per program run; -999 = nothing pending.
		game_view_consume_pending_launch();
	}

	game_instance_manager()->shutdown();
	player_manager()->shutdown();
	font_cache()->shutdown();
	rasterizer()->shutdown();
	console_logger()->shutdown();

	return 0;
}

LRESULT window_process_message(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	LRESULT result;

	if (uMsg == WM_SIZE) {
		if (wParam == SIZE_MINIMIZED) {
			return 0;
		}
		g_win32_parameter.window_width = LOWORD(lParam);
		g_win32_parameter.window_height = HIWORD(lParam);
	}

	result = game_instance_manager()->process_message(hWnd, uMsg, wParam, lParam);

	if (result) {
		return 0;
	}

	switch (uMsg) {
	case WM_SIZE: {
		request_resize = true;
		return 0;
	}
	case WM_CLOSE: {
		DestroyWindow(hWnd);
		return 0;
	}
	case WM_DESTROY: {
		PostQuitMessage(0);
		return 0;
	}
	default:
		break;
	}

	return CallWindowProcW(DefWindowProcW, hWnd, uMsg, wParam, lParam);
}
