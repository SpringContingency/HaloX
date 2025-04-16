#include "imgui_player_view.h"

#include "../player/player_manager.h"

using namespace libmcc;
using namespace ImGui;

static char name[4];
static char service_tag[4];
static e_local_player selected_player = _local_player_0;
static e_module game = _module_halo1;
constexpr const char* game_names[] = {
	"halo1",
	"halo2",
	"halo3",
	"halo4",
	"groundhog",
	"halo3odst",
	"haloreach"
};

constexpr const char* player_names[] = {
	"Player 0",
	"Player 1",
	"Player 2",
	"Player 3",
};

bool c_imgui_player_view::begin(bool* show) {
	return Begin("player", show);
}

void c_imgui_player_view::render() {
	auto manager = player_manager();

	auto player_count = manager->get_player_count();

	if (InputInt("player count", &player_count, 1) &&
		player_count >= 1 &&
		player_count <= k_local_player_count
		) {
		manager->set_player_count(player_count);
	}

	if (BeginCombo("Player", player_names[selected_player])) {
		for (int i = 0; i < k_local_player_count; ++i) {
			if (Selectable(player_names[i], i == selected_player)) {
				selected_player = static_cast<e_local_player>(i);
			}
		}

		EndCombo();
	}

	if (CollapsingHeader("Info")) {
		render_info();
	}

	if (CollapsingHeader("Customization")) {
		render_customization();
	}

	if (CollapsingHeader("Setting")) {
		render_setting();
	}
}

void c_imgui_player_view::end() {
	End();
}

void c_imgui_player_view::render_info() {
}

void c_imgui_player_view::render_customization() {
	if (BeginCombo("Game", game_names[game])) {

		for (int i = 0; i < k_game_count; ++i) {
			if (Selectable(game_names[i], game == i)) {
				game = static_cast<e_module>(i);
			}
		}

		EndCombo();
	}
}

void c_imgui_player_view::render_setting() {
	if (BeginTabBar("Settings")) {
		if (BeginTabItem("Input")) {
			EndTabItem();
		}

		if (BeginTabItem("Video")) {
			EndTabItem();
		}

		if (BeginTabItem("Audio")) {
			EndTabItem();
		}

		if (BeginTabItem("Game")) {
			EndTabItem();
		}

		if (BeginTabItem("Misc")) {
			EndTabItem();
		}

		EndTabBar();
	}
}

static void main_render_player_info() {
	if (InputText("Name", name, sizeof(name))) {

	}

	if (InputText("Service Tag", service_tag, sizeof(service_tag))) {

	}
}
