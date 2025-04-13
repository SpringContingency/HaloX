#include "main.h"

#include <imgui.h>

void main_render() {
	static bool show_demo = false;
	static bool show_player = false;

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

	main_render_game();

	if (show_demo)
		ImGui::ShowDemoWindow(&show_demo);

	if (show_player)
		main_render_player(&show_player);
}
