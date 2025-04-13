#include "main.h"

#include <imgui.h>

#include "../player/player_manager.h"

using namespace libmcc;
using namespace ImGui;

static char name[4];
static char service_tag[4];
static e_local_player selected_player = _local_player_0;

constexpr const char* player_names[] = {
	"Player 0",
	"Player 1",
	"Player 2",
	"Player 3",
};

static void main_render_player_info() {
	if (InputText("Name", name, sizeof(name))) {

	}

	if (InputText("Service Tag", service_tag, sizeof(service_tag))) {

	}
}

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

static void main_render_player_customization() {
	if (BeginCombo("Game", game_names[game])) {

		for (int i = 0; i < k_game_count; ++i) {
			if (Selectable(game_names[i], game == i)) {
				game = static_cast<e_module>(i);
			}
		}

		EndCombo();
	}
}

static void main_render_player_setting() {
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

static void main_render_player_internal() {
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
		main_render_player_info();
	}

	if (CollapsingHeader("Customization")) {
		main_render_player_customization();
	}

	if (CollapsingHeader("Setting")) {
		main_render_player_setting();
	}
}

void main_render_player(bool* show) {
	if (Begin("player", show)) {
		main_render_player_internal();
	}
	End();
}
