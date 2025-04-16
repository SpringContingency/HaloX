#include "imgui_game_view.h"

#include "../rasterizer/rasterizer.h"
#include "../game/game_instance_manager.h"

using namespace libmcc;

static s_game_prop prop;

constexpr const char* game_names[] = {
	"Halo1",
	"Halo2",
	"Halo3",
	"Halo4",
	"Halo2A",
	"Halo3 ODST",
	"Halo Reach"
};
constexpr const char* game_mode_names[] = {
	"",
	"Campaign",
	"Spartan Ops",
	"Multiplayer",
	"UI Shell",
	"Firefight"
};

constexpr const char* game_difficulty_names[] = {
	"Easy",
	"Normal",
	"Hard",
	"Impossible"
};

void c_imgui_game_view::render() {
	if (ImGui::Begin("Game Window")) {
		render_internal();
	}
	ImGui::End();
}

void c_imgui_game_mainmenu_view::render_internal() {
	// title
	auto selected_title = prop.module == k_module_none ? "" : game_names[prop.module];
	if (ImGui::BeginCombo("Title", selected_title)) {
		for (int i = _module_halo1; i < k_game_count; ++i) {
			if (ImGui::Selectable(game_names[i], prop.module == i)) {
				prop.module = static_cast<e_module>(i);
			}
		}
		ImGui::EndCombo();
	}

	// game mode
	if (ImGui::BeginCombo("Mode", game_mode_names[prop.mode])) {
		for (int i = _game_mode_campaign; i <= _game_mode_firefight; ++i) {
			if (ImGui::Selectable(game_mode_names[i], prop.mode == i)) {
				prop.mode = static_cast<e_game_mode>(i);
			}
		}
		ImGui::EndCombo();
	}

	// map
	ImGui::InputInt("Map", reinterpret_cast<int*>(&prop.map.builtin_map_id));

	switch (prop.mode) {
	case _game_mode_campaign: {
		render_campaign();
		break;
	}
	case _game_mode_multiplayer: {
		render_multiplayer();
		break;
	}
	default:
		break;
	}

	// collapse advance options
	if (ImGui::CollapsingHeader("Advanced Options")) {
		ImGui::InputInt("Game Tick", &prop.game_tick);
	}

	// launch
	if (ImGui::Button("Launch")) {
		game_instance_manager()->launch_game(&prop);
	}
}

void c_imgui_game_mainmenu_view::render_campaign() {
	const char* preview;

	preview = prop.difficulty == k_campaign_difficulty_level_none ?
		"" :
		game_difficulty_names[prop.difficulty];

	if (ImGui::BeginCombo("Game Difficulty", preview)) {
		for (int i = 0; i < k_campaign_difficulty_level_count; ++i) {
			if (ImGui::Selectable(game_difficulty_names[i], prop.difficulty == i)) {
				prop.difficulty = static_cast<e_campaign_difficulty_level>(i);
			}
		}
		ImGui::EndCombo();
	}
}

void c_imgui_game_mainmenu_view::render_multiplayer() {
	const s_game_local* local = nullptr;
	const char* preview_value = nullptr;

	if (prop.module != k_module_none) {
		local = game_instance_manager()->get_game_locals(prop.module);
	}

	if (local &&
		prop.hopper_game_variant >= 0 &&
		prop.hopper_game_variant <= local->hopper_game_variants.size()
		) {
		preview_value = local->hopper_game_variants[prop.hopper_game_variant].c_str();
	}

	if (ImGui::BeginCombo("Game Variant", preview_value)) {
		if (local)
			for (int i = 0; i < local->hopper_game_variants.size(); ++i)
				if (ImGui::Selectable(local->hopper_game_variants[i].c_str(), prop.hopper_game_variant == i))
					prop.hopper_game_variant = i;
		ImGui::EndCombo();
	}

	switch (prop.module) {
	case _module_halo1:
	case _module_halo2:
	case _module_halo3odst:
		return;
	default:
		break;
	}

	preview_value = nullptr;

	if (local &&
		prop.hopper_map_variant >= 0 &&
		prop.hopper_map_variant <= local->hopper_map_variants.size()
		) {
		preview_value = local->hopper_map_variants[prop.hopper_map_variant].c_str();
	}

	if (ImGui::BeginCombo("Map Variant", preview_value)) {
		if (local)
			for (int i = 0; i < local->hopper_map_variants.size(); ++i)
				if (ImGui::Selectable(local->hopper_map_variants[i].c_str(), prop.hopper_map_variant == i))
					prop.hopper_map_variant = i;
		ImGui::EndCombo();
	}
}

void c_imgui_game_ingame_view::render_internal() {
	auto window = ImGui::GetCurrentWindow();

	auto set_shader = [](const ImDrawList*, const ImDrawCmd*) {
		rasterizer()->set_shader(_shader_simple);
		};

	window->DrawList->AddCallback(set_shader, nullptr);

	auto surface = rasterizer()->get_surface(_surface_game);

	auto p0 = window->WorkRect.Min - window->WindowPadding;
	auto p1 = window->WorkRect.Max + window->WindowPadding;

	window->DrawList->AddImage(
		reinterpret_cast<ImTextureID>(surface->shader_resource_view),
		p0,
		p1,
		ImVec2(0, 0),
		ImVec2(1, 1),
		IM_COL32_WHITE);

	ImGui::IsItemClicked();

	window->DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
