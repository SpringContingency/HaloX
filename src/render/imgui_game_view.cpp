#include "imgui_game_view.h"

#include "../rasterizer/rasterizer.h"
#include "../game/game_instance_manager.h"
#include "../game/map_names.h"
#include "../game/mcc_user_settings.h"
#include "../logging/logging.h"
#include "../main/main.h"
#include "../network/instance_discovery.h"
#include "../ui/ui_chrome.h"
#include "../ui/ui_theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace libmcc;

namespace {
namespace fs = std::filesystem;

// Persisted launcher dropdown state. Stored as key=value lines next to the
// halox root folder so it survives across program runs. Saved on every change.
fs::path launcher_prop_path() {
    return fs::path(main_get_root_folder()) / L"launcher_prop.ini";
}

void launcher_prop_load(s_game_prop& p) {
    std::ifstream f(launcher_prop_path());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
        if      (k == "module")              p.module              = (e_module)atoi(v.c_str());
        else if (k == "mode")                p.mode                = (e_game_mode)atoi(v.c_str());
        else if (k == "difficulty")          p.difficulty          = (e_campaign_difficulty_level)atoi(v.c_str());
        else if (k == "map_id")              p.map.builtin_map_id  = (e_map_id)atoi(v.c_str());
        else if (k == "hopper_game_variant") p.hopper_game_variant = atoi(v.c_str());
        else if (k == "hopper_map_variant")  p.hopper_map_variant  = atoi(v.c_str());
        else if (k == "film")                p.film                = atoi(v.c_str());
        else if (k == "game_tick")           p.game_tick           = atoi(v.c_str());
        else if (k == "skull_flags")         p.skull_flags         = strtoull(v.c_str(), nullptr, 10);
        else if (k == "insertion_point")     p.campaign_insertion_point = atoi(v.c_str());
    }
    // Always re-stamp the magic flags — without 0x8888 builtin_map_id is rejected.
    p.map.flags = 0x8888;
    p.map.part1 = 0;
    p.map.part2 = 0;
}

void launcher_prop_save(const s_game_prop& p) {
    std::error_code ec;
    fs::create_directories(launcher_prop_path().parent_path(), ec);
    fs::path tmp = launcher_prop_path();
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << "module="              << (int)p.module              << "\n"
          << "mode="                << (int)p.mode                << "\n"
          << "difficulty="          << (int)p.difficulty          << "\n"
          << "map_id="              << (int)p.map.builtin_map_id  << "\n"
          << "hopper_game_variant=" << p.hopper_game_variant      << "\n"
          << "hopper_map_variant="  << p.hopper_map_variant       << "\n"
          << "film="                << p.film                     << "\n"
          << "game_tick="           << p.game_tick                << "\n"
          << "skull_flags="         << p.skull_flags              << "\n"
          << "insertion_point="     << p.campaign_insertion_point << "\n";
    }
    fs::rename(tmp, launcher_prop_path(), ec);
    if (ec) {
        fs::remove(tmp, ec);
        std::ofstream f(launcher_prop_path(), std::ios::trunc);
        if (f) {
            f << "module="              << (int)p.module              << "\n"
              << "mode="                << (int)p.mode                << "\n"
              << "difficulty="          << (int)p.difficulty          << "\n"
              << "map_id="              << (int)p.map.builtin_map_id  << "\n"
              << "hopper_game_variant=" << p.hopper_game_variant      << "\n"
              << "hopper_map_variant="  << p.hopper_map_variant       << "\n"
              << "film="                << p.film                     << "\n"
              << "game_tick="           << p.game_tick                << "\n"
              << "skull_flags="         << p.skull_flags              << "\n"
              << "insertion_point="     << p.campaign_insertion_point << "\n";
        }
    }
}

bool launcher_prop_changed(const s_game_prop& a, const s_game_prop& b) {
    return a.module != b.module
        || a.mode != b.mode
        || a.difficulty != b.difficulty
        || a.map.builtin_map_id != b.map.builtin_map_id
        || a.hopper_game_variant != b.hopper_game_variant
        || a.hopper_map_variant != b.hopper_map_variant
        || a.film != b.film
        || a.game_tick != b.game_tick
        || a.skull_flags != b.skull_flags
        || a.campaign_insertion_point != b.campaign_insertion_point;
}
} // namespace

// Sane initial values so the very first Launch click on a fresh halox.exe lands
// on Halo 3 Sierra 117 — campaign mode, normal difficulty, valid map id. After
// that, prop is overwritten by whatever launcher_prop.ini holds (the user's
// last-used selection, persisted on every dropdown change).
static s_game_prop prop = []() {
	s_game_prop p;
	p.module = _module_halo3;
	p.mode   = _game_mode_campaign;
	p.difficulty = _campaign_difficulty_level_normal;
	p.map.builtin_map_id = _map_id_halo3_sierra_117;
	p.map.flags          = 0x8888;
	launcher_prop_load(p);
	return p;
}();
static s_game_prop last_saved_prop = prop;
static int last_launch_rc = 0;

// Programmatic-launch state. game_view_request_launch_from_spec stamps these,
// game_view_consume_pending_launch fires the launch and clears them.
static bool         g_pending_launch     = false;
static s_game_prop  g_pending_launch_prop{};

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

// Display names for libmcc::e_skull (parallel to the enum, indices 0..k_skull_count-1).
// Order MUST match the enum in libmcc/game/game_options.h. Not every skull is
// recognized by every engine — extras are silently ignored at gameplay time,
// so it's safe to surface the full list to the user.
struct s_skull_label {
	const char* short_name;  // checkbox label
	const char* desc;        // tooltip explaining the gameplay effect
};
constexpr s_skull_label k_skull_labels[] = {
	{ "Anger",                    "Enemies fire faster and more aggressively." },
	{ "Assassins",                "All enemies have active camo." },
	{ "Bandanna",                 "Infinite ammo and grenades, no reload." },
	{ "Black Eye",                "Shields don't recharge unless you melee enemies." },
	{ "Blind",                    "HUD and first-person weapon are hidden." },
	{ "Bonded Pair",              "Each AI is tethered to a partner; killing one weakens the other." },
	{ "Boom",                     "Doubled explosion radius and force." },
	{ "Catch",                    "Enemies throw and drop more grenades." },
	{ "Cowbell",                  "Acceleration from explosions is increased." },
	{ "Envy",                     "Hold the flashlight key to use active camo." },
	{ "Eye Patch",                "Disables auto-aim assist." },
	{ "Famine",                   "Dropped weapons have less ammo." },
	{ "Feather",                  "Player jumps higher." },
	{ "Fog",                      "Motion tracker is disabled." },
	{ "Foreign",                  "Cannot use covenant weapons." },
	{ "Ghost",                    "Allies are invulnerable." },
	{ "Grunt Birthday Party",     "Headshots on grunts produce confetti and cheers." },
	{ "Grunt Funeral",            "Grunts explode on death." },
	{ "Iron",                     "Co-op: any death restarts at the last checkpoint." },
	{ "IWHBYD",                   "Rare combat dialogue triggers more often." },
	{ "Jacked",                   "Enemies hijack your vehicles." },
	{ "Malfunction",              "Random HUD elements glitch off." },
	{ "Masterblaster",            "All weapons fire explosive rounds." },
	{ "Mythic",                   "Enemies have double health." },
	{ "Pinata",                   "Punching enemies drops more grenades." },
	{ "Prophet Birthday Party",   "Prophets headshot with confetti effects." },
	{ "Recession",                "All weapons fire backward." },
	{ "Scarab",                   "All weapons act like the Scarab cannon." },
	{ "So Angry",                 "Enemies become enraged on damage." },
	{ "Sputnik",                  "Object mass is reduced — explosions and melee fling things farther." },
	{ "Streaking",                "Player movement leaves a visual trail." },
	{ "Swarm",                    "More enemies in encounters." },
	{ "That's Just Wrong",        "Enemies see through camo and stealth." },
	{ "They Come Back",           "Killed enemies revive after a delay." },
	{ "Thunderstorm",             "All enemies upgraded one rank." },
	{ "Tilt",                     "Damage type effectiveness against shields/health flipped." },
	{ "Tough Luck",               "Enemies always dive away from grenades and dodge fire." },
	{ "Boots Off The Ground",     "All combat takes place in vehicles where possible." },
};
static_assert(sizeof(k_skull_labels) / sizeof(k_skull_labels[0]) == libmcc::k_skull_count,
	"k_skull_labels must stay in sync with libmcc::e_skull");

void c_imgui_game_view::render() {
	if (ImGui::Begin("Game Window")) {
		render_internal();
	}
	ImGui::End();
}

void c_imgui_game_ingame_view::render() {
	// Fullscreen, chromeless backdrop window for the live game surface.
	// Sits at z-order zero so the pause overlay (drawn by the shell) and
	// any per-game HUD (halo3 view) layer cleanly on top.
	const ImVec2 win_pos(0.0f, 0.0f);
	const ImVec2 win_size((float)g_win32_parameter.window_width,
	                      (float)g_win32_parameter.window_height);
	ImGui::SetNextWindowPos(win_pos);
	ImGui::SetNextWindowSize(win_size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoSavedSettings;
	if (ImGui::Begin("##halox_game_surface", nullptr, flags)) {
		render_internal();
	}
	ImGui::End();
	ImGui::PopStyleVar(3);
}

void c_imgui_game_mainmenu_view::render_internal() {
	// Snapshot module/mode so we can detect changes and re-seed the map.
	auto prev_module = prop.module;
	auto prev_mode   = prop.mode;

	// title
	auto selected_title = prop.module == k_module_none ? "" : game_names[prop.module];
	if (ImGui::BeginCombo("Title", selected_title)) {
		for (int i = _module_halo1; i < k_game_count; ++i) {
			if (ImGui::Selectable(game_names[i], prop.module == i)) {
				prop.module = static_cast<e_module>(i);
				apply_mcc_settings_for_module(prop.module);
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

	// If module or mode changed, OR the current map doesn't fit the new
	// (module, mode) pair, auto-pick the first matching entry. Avoids leaving
	// the user staring at "Sierra 117" after switching to MP / Reach.
	bool need_reseed = (prev_module != prop.module) || (prev_mode != prop.mode);
	if (!need_reseed) {
		// Validate current selection still matches.
		bool current_fits = false;
		for (auto& e : k_map_entries) {
			if (e.id == prop.map.builtin_map_id) {
				current_fits = (e.module == prop.module) && map_entry_matches_mode(e, prop.mode);
				break;
			}
		}
		need_reseed = !current_fits;
	}
	if (need_reseed) {
		auto first = find_first_map_for(prop.module, prop.mode);
		if (first != (libmcc::e_map_id)(-1)) prop.map.builtin_map_id = first;
	}

	// Map dropdown — filtered to entries belonging to the currently selected
	// (game, mode). Always stamp flags=0x8888 so libmcc::s_scenario_map_id::is_builtin()
	// is true; without that, halo3 returns "no map selected".
	prop.map.part1 = 0;
	prop.map.flags = 0x8888;
	prop.map.part2 = 0;
	{
		const char* preview = find_map_name(prop.map.builtin_map_id);
		if (ImGui::BeginCombo("Map", preview)) {
			for (int i = 0; i < k_map_entry_count; ++i) {
				const auto& e = k_map_entries[i];
				if (prop.module != k_module_none && e.module != prop.module) continue;
				if (!map_entry_matches_mode(e, prop.mode)) continue;
				bool selected = (e.id == prop.map.builtin_map_id);
				char label[160];
				snprintf(label, sizeof(label), "%s (id=%d)", e.name, (int)e.id);
				if (ImGui::Selectable(label, selected)) {
					prop.map.builtin_map_id = e.id;
					// Snap the map-variant selection to the first variant
					// known to belong to the new map. Without this the
					// dropdown still proudly shows an mvar from the previous
					// map, which won't actually load.
					auto* l = (prop.module != k_module_none)
						? game_instance_manager()->get_game_locals(prop.module)
						: nullptr;
					if (l) {
						const int legacy = find_legacy_map_id(e.id);
						if (legacy != -1) {
							bool current_ok = false;
							if (prop.hopper_map_variant >= 0 &&
								prop.hopper_map_variant < (int)l->hopper_map_variant_ids.size()) {
								int cur_id = l->hopper_map_variant_ids[prop.hopper_map_variant];
								current_ok = (cur_id == legacy);
							}
							if (!current_ok) {
								prop.hopper_map_variant = -1;
								for (int j = 0; j < (int)l->hopper_map_variant_ids.size(); ++j) {
									if (l->hopper_map_variant_ids[j] == legacy) {
										prop.hopper_map_variant = j;
										break;
									}
								}
							}
						}
					}
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled("map_id=%d", (int)prop.map.builtin_map_id);
	}

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

	// Launch — sized larger and framed with cyan corner brackets so it
	// reads as the primary call-to-action (Halo CE reticle vibe).
	{
		const ImVec2 sz(160.0f, 36.0f);
		ImVec2 cur = ImGui::GetCursorScreenPos();
		bool clicked = ImGui::Button("LAUNCH", sz);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		halox::ui::chrome_corner_brackets(
			dl,
			ImVec2(cur.x - 4.0f, cur.y - 4.0f),
			ImVec2(cur.x + sz.x + 4.0f, cur.y + sz.y + 4.0f),
			halox::ui::k_bracket_len,
			halox::ui::k_border_strong,
			ImGui::ColorConvertFloat4ToU32(halox::ui::col_accent));
		if (clicked) {
			apply_mcc_settings_for_module(prop.module);
			last_launch_rc = game_instance_manager()->launch_game(&prop);
			if (last_launch_rc == 0) {
				halox::network::instance_discovery_set_session(
					(int)prop.module, (int)prop.mode, (int)prop.map.builtin_map_id,
					prop.hopper_game_variant, prop.hopper_map_variant,
					(int)prop.difficulty, find_map_name(prop.map.builtin_map_id));
			}
		}
	}
	if (last_launch_rc != 0) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
			"launch_game returned %d — see halox.log for the reason", last_launch_rc);
	}

	// Persist any change the user made this frame so the dropdowns come back
	// the same way next time halox.exe is launched.
	if (launcher_prop_changed(prop, last_saved_prop)) {
		launcher_prop_save(prop);
		last_saved_prop = prop;
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

	// Insertion point — engine-internal checkpoint index. 0 = mission start;
	// non-zero jumps to a later checkpoint within the scenario. Range varies
	// per game/level, so we expose a free-form integer rather than guess limits.
	ImGui::InputInt("Insertion Point", &prop.campaign_insertion_point);
	if (prop.campaign_insertion_point < 0) prop.campaign_insertion_point = 0;
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("0 = start of mission. Higher values jump to later checkpoints in the scenario (per-game, per-level).");
	}

	// Skulls — each bit toggles one libmcc::e_skull entry on the launch
	// game_options. Two-column layout keeps the panel compact even with the
	// full ~38-skull list. Hidden inside a CollapsingHeader so it doesn't
	// dominate the campaign panel for users who never touch them.
	{
		int active = 0;
		for (int i = 0; i < libmcc::k_skull_count; ++i) {
			if (prop.skull_flags & (1ull << i)) ++active;
		}
		char header[64];
		// ### marker — everything AFTER ### is the stable ID; everything
		// before is just display text. Plain ## would still hash the changing
		// "(N active)" prefix and reset the open-state on every toggle.
		snprintf(header, sizeof(header), "Skulls (%d active)###skulls_header", active);
		if (ImGui::CollapsingHeader(header)) {
			if (ImGui::SmallButton("Clear All##skulls")) prop.skull_flags = 0;
			ImGui::SameLine();
			ImGui::TextDisabled("Toggle gameplay modifiers — only those supported by the selected game take effect.");

			ImGui::Columns(2, "skull_cols", false);
			for (int i = 0; i < libmcc::k_skull_count; ++i) {
				const uint64_t mask = 1ull << i;
				bool on = (prop.skull_flags & mask) != 0;
				ImGui::PushID(i);
				if (ImGui::Checkbox(k_skull_labels[i].short_name, &on)) {
					if (on) prop.skull_flags |=  mask;
					else    prop.skull_flags &= ~mask;
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", k_skull_labels[i].desc);
				ImGui::PopID();
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}
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
		if (local) {
			// mvars store the engine-internal "legacy" map id (Reflection=1150),
			// not the libmcc enum value (Reflection=194). Translate via the
			// table in map_names.h. -1 means we have no legacy id for this map,
			// so the filter degrades to "show everything" instead of hiding
			// the entire list.
			const int sel_legacy = find_legacy_map_id(prop.map.builtin_map_id);
			int shown = 0;
			for (int i = 0; i < (int)local->hopper_map_variants.size(); ++i) {
				int mvid = (i < (int)local->hopper_map_variant_ids.size())
					? local->hopper_map_variant_ids[i] : -1;
				if (sel_legacy != -1 && mvid != sel_legacy) continue;
				if (ImGui::Selectable(local->hopper_map_variants[i].c_str(), prop.hopper_map_variant == i))
					prop.hopper_map_variant = i;
				shown++;
			}
			if (shown == 0)
				ImGui::TextDisabled("No map variants for this map");
		}
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

	D3D11_TEXTURE2D_DESC desc;
	surface->texture->GetDesc(&desc);

	const float image_width = desc.Width > 0 ? desc.Width : 1;
	const float image_height = desc.Height > 0 ? desc.Height : 1;
	const float image_aspect = image_width / image_height;

	const ImVec2 work_p0 = window->WorkRect.Min - window->WindowPadding;
	const ImVec2 work_p1 = window->WorkRect.Max + window->WindowPadding;
	const ImVec2 available_size = work_p1 - work_p0;
	const float available_aspect = available_size.x / available_size.y;

	ImVec2 draw_size;
	if (available_aspect > image_aspect) {
		draw_size.y = available_size.y;
		draw_size.x = draw_size.y * image_aspect;
	} else {
		draw_size.x = available_size.x;
		draw_size.y = draw_size.x / image_aspect;
	}

	const ImVec2 draw_center = work_p0 + (available_size - draw_size) * 0.5f;
	const ImVec2 p0 = draw_center;
	const ImVec2 p1 = draw_center + draw_size;

	// Always draw the (frozen, since the engine is paused) game surface sharp.
	window->DrawList->AddImage(
		reinterpret_cast<ImTextureID>(surface->resource),
		p0, p1,
		ImVec2(0, 0), ImVec2(1, 1),
		IM_COL32_WHITE);

	// Pause-overlay dim: a single full-screen rect in the load-screen theme
	// color (col_bg = #06080A) at ~70% alpha. Replaces the previous
	// pseudo-blur attempts — the user just wants a flat semi-transparent
	// scrim that matches the rest of the halox chrome.
	if (g_show_imgui) {
		ImVec4 dim = halox::ui::col_bg;
		dim.w = 0.70f;
		window->DrawList->AddRectFilled(
			p0, p1,
			ImGui::ColorConvertFloat4ToU32(dim));
	}

	ImGui::IsItemClicked();

	window->DrawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

// --- Programmatic launch parser ----------------------------------------------

namespace {

// Case-insensitive equal.
bool ieq(const char* a, const char* b) {
	if (!a || !b) return false;
	while (*a && *b) {
		char ca = *a, cb = *b;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
		if (ca != cb) return false;
		++a; ++b;
	}
	return *a == 0 && *b == 0;
}

bool parse_module(const char* s, e_module& out) {
	if (ieq(s, "halo1"))                          { out = _module_halo1;      return true; }
	if (ieq(s, "halo2"))                          { out = _module_halo2;      return true; }
	if (ieq(s, "halo3"))                          { out = _module_halo3;      return true; }
	if (ieq(s, "halo4"))                          { out = _module_halo4;      return true; }
	if (ieq(s, "halo2a") || ieq(s, "groundhog"))  { out = _module_groundhog;  return true; }
	if (ieq(s, "halo3odst") || ieq(s, "odst"))    { out = _module_halo3odst;  return true; }
	if (ieq(s, "haloreach") || ieq(s, "reach"))   { out = _module_haloreach;  return true; }
	return false;
}

bool parse_mode(const char* s, e_game_mode& out) {
	if (ieq(s, "campaign"))                            { out = _game_mode_campaign;     return true; }
	if (ieq(s, "multiplayer") || ieq(s, "mp"))         { out = _game_mode_multiplayer;  return true; }
	if (ieq(s, "firefight")    || ieq(s, "ff"))        { out = _game_mode_firefight;    return true; }
	if (ieq(s, "spartan_ops")  || ieq(s, "spartanops")){ out = _game_mode_spartan_ops;  return true; }
	if (ieq(s, "ui_shell")     || ieq(s, "uishell"))   { out = _game_mode_ui_shell;     return true; }
	return false;
}

bool parse_difficulty(const char* s, e_campaign_difficulty_level& out) {
	if (ieq(s, "easy"))       { out = _campaign_difficulty_level_easy;       return true; }
	if (ieq(s, "normal"))     { out = _campaign_difficulty_level_normal;     return true; }
	if (ieq(s, "hard") || ieq(s, "heroic"))
	                          { out = _campaign_difficulty_level_hard;       return true; }
	if (ieq(s, "impossible") || ieq(s, "legendary"))
	                          { out = _campaign_difficulty_level_impossible; return true; }
	return false;
}

}  // namespace

bool game_view_request_launch_from_spec(const char* spec) {
	if (!spec || !*spec) return false;

	// Split on ':' into up to 5 fields.
	char buf[512];
	std::strncpy(buf, spec, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;

	const char* fields[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
	int n = 0;
	fields[n++] = buf;
	for (char* p = buf; *p && n < 5; ++p) {
		if (*p == ':') { *p = 0; fields[n++] = p + 1; }
	}
	if (n < 3) {
		CONSOLE_LOG_ERROR("--launch: need at least <module>:<mode>:<map> (got '%s')", spec);
		return false;
	}

	s_game_prop p{};
	if (!parse_module(fields[0], p.module)) {
		CONSOLE_LOG_ERROR("--launch: unknown module '%s'", fields[0]);
		return false;
	}
	if (!parse_mode(fields[1], p.mode)) {
		CONSOLE_LOG_ERROR("--launch: unknown mode '%s'", fields[1]);
		return false;
	}
	auto map_id = find_map_id_by_name_fragment(fields[2], p.module);
	if (map_id == (libmcc::e_map_id)(-1)) {
		CONSOLE_LOG_ERROR("--launch: no map matching '%s' for module %s",
			fields[2], fields[0]);
		return false;
	}
	p.map.builtin_map_id = map_id;
	p.map.flags          = 0x8888;
	p.difficulty         = _campaign_difficulty_level_normal;
	if (n >= 4 && fields[3] && *fields[3]) {
		if (!parse_difficulty(fields[3], p.difficulty)) {
			CONSOLE_LOG_WARN("--launch: unknown difficulty '%s' (using normal)", fields[3]);
		}
	}
	p.hopper_game_variant = -1;
	p.hopper_map_variant  = -1;
	if (n >= 5 && fields[4] && *fields[4]) {
		p.hopper_game_variant = atoi(fields[4]);
	}

	g_pending_launch_prop = p;
	g_pending_launch      = true;

	CONSOLE_LOG_INFO("--launch queued: module=%d mode=%d map=%d (%s) diff=%d variant=%d",
		(int)p.module, (int)p.mode, (int)map_id, find_map_name(map_id),
		(int)p.difficulty, p.hopper_game_variant);
	return true;
}

int game_view_consume_pending_launch() {
	if (!g_pending_launch) return -999;
	g_pending_launch = false;
	// Stamp the user-facing prop too so the menu reflects what was launched.
	prop = g_pending_launch_prop;
	apply_mcc_settings_for_module(prop.module);
	int rc = game_instance_manager()->launch_game(&prop);
	last_launch_rc = rc;
	if (rc != 0) {
		CONSOLE_LOG_ERROR("--launch: launch_game returned %d (see halox.log)", rc);
	} else {
		halox::network::instance_discovery_set_session(
			(int)prop.module, (int)prop.mode, (int)prop.map.builtin_map_id,
			prop.hopper_game_variant, prop.hopper_map_variant,
			(int)prop.difficulty, find_map_name(prop.map.builtin_map_id));
	}
	return rc;
}
