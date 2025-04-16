#include "main.h"

#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"
#include "../player/player_manager.h"
#include "../game/game_instance_manager.h"

#include "../render/imgui_main_view.h"
#include "../render/imgui_game_view.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

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

	console_logger()->initialize();
	rasterizer()->initialize();
	player_manager()->initialize();
	game_instance_manager()->initialize();

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
			SwitchToThread();
			continue;
		}

		rasterizer()->begin_frame();
		c_imgui_main_view().render();
		c_imgui_game_mainmenu_view().render();
		rasterizer()->end_frame();
	}

	game_instance_manager()->shutdown();
	player_manager()->shutdown();
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
