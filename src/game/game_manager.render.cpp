#include "game_manager.h"
#include "game_instance_manager.h"
#include "../rasterizer/rasterizer.h"
#include "../render/imgui_main_view.h"
#include "../render/imgui_game_view.h"
#include "../render/imgui_game_halo3_view.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>

using namespace libmcc;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void imgui_process_message(const MSG* msg) {
	ImGui_ImplWin32_WndProcHandler(
		g_win32_parameter.window_handle,
		msg->message,
		msg->wParam,
		msg->lParam);
}

void __fastcall c_game_manager::begin_frame() {
	g_show_imgui_cached = g_show_imgui;

	if (g_show_imgui_cached) {
		if (!g_game_focused) {
			g_message_queue.flush(imgui_process_message);
		}
		rasterizer()->begin_frame();
	}
}

static c_imgui_game_halo3_view imgui_game_halo3_view;

static c_imgui_view* game_views[k_game_count] = {
	nullptr,
	nullptr,
	&imgui_game_halo3_view,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
};

void __fastcall c_game_manager::end_frame(
	IDXGISwapChain* swapchain, 
	UINT* flags
) {
	if (g_show_imgui_cached) {
		c_imgui_main_view().render();
		c_imgui_game_ingame_view().render();
		auto game = game_instance_manager()->get_game();
		auto game_view = game_views[game];
		if (game_view) game_view->render();
		rasterizer()->end_frame();
	}
}

void __fastcall c_game_manager::resize() {
	SendMessageW(
		g_win32_parameter.window_handle, 
		_window_message_game_resize, 
		0, 
		0);
}
