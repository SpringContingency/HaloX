#include "imgui_main_view.h"
#include "imgui_player_view.h"
#include "../main/main.h"

void c_imgui_main_view::render() {
	static bool show_demo = false;
	static bool show_player = false;
	extern bool g_font_cache_failed;
	extern bool g_exit;

	if (g_font_cache_failed) {
		if (ImGui::Begin("Error")) {
			ImGui::TextUnformatted("Failed to initalize font cache.");
			if (ImGui::Button("Close")) {
				g_exit = true;
			}
		}
		ImGui::End();
		return;
	}

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("...")) {
			ImGui::MenuItem("demo", nullptr, &show_demo);
			if (ImGui::MenuItem("exit")) {
				PostMessageW(g_win32_parameter.window_handle, _window_message_game_quit, NULL, NULL);
			}
			ImGui::EndMenu();
		}
		ImGui::MenuItem("player", nullptr, &show_player);
		ImGui::EndMainMenuBar();
	}

	if (show_demo)
		ImGui::ShowDemoWindow(&show_demo);

	if (show_player) {
		c_imgui_player_view imgui_player_view;
		if (imgui_player_view.begin(&show_player)) {
			imgui_player_view.render();
		}
		imgui_player_view.end();
	}
}
