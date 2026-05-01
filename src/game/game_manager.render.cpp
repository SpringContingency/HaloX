#include "game_manager.h"
#include "game_instance_manager.h"
#include "halo2_native_overrides.h"
#include "halo3_native_overrides.h"
#include "haloreach_native_overrides.h"
#include "../diag/launch_liveness.h"
#include "../rasterizer/rasterizer.h"
#include "../render/imgui_main_view.h"
#include "../render/imgui_game_view.h"
#include "../render/imgui_game_halo3_view.h"
#include "../ui/ui_launch.h"
#include "../ui/ui_progress.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

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

	// halo3 / haloreach read bindings/FOV from their own per-player caches,
	// not from libmcc's profile struct. Stamp our values per-frame after the
	// cache has been validated (no-op until then). Cheap (4 entries × 1 flag
	// check + small loop). Each writer no-ops when its game isn't the active
	// module so this safely runs every frame.
	halo2_apply_native_overrides();
	halo3_apply_native_overrides();
	haloreach_apply_native_overrides();

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
		// Liveness probe must run on the game thread — not the UI
		// thread — because it touches the d3d11 immediate context
		// (CopyResource + Map). Calling it from the UI thread races the
		// game thread's d3d11 work and AVs in d3d11.dll. Driving from
		// here keeps the immediate context single-producer.
		liveness_probe_tick();

		// Order matters: game surface FIRST as the fullscreen backdrop,
		// then per-game HUD overlay, then the shell on top (so the pause
		// overlay panel layers cleanly above the game). During the load
		// phase we skip the game surface + per-game HUD — the shell's
		// full-screen loading screen owns the window.
		const bool loading =
			halox::ui::g_launch_in_progress ||
			halox::ui::progress_is_loading();
		if (!loading) {
			c_imgui_game_ingame_view().render();
			auto game = game_instance_manager()->get_game();
			auto game_view = game_views[game];
			if (game_view) game_view->render();
		}
		c_imgui_main_view().render();
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
