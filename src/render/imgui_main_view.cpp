#include "imgui_main_view.h"
#include "imgui_player_view.h"
#include "imgui_session_browser_view.h"
#include "imgui_game_view.h"
#include "../main/main.h"
#include "../input/win32_input.h"
#include "../game/mcc_user_settings.h"
#include "../game/halox_audio.h"
#include "../game/game_instance_manager.h"
#include "../game/halo2_native_overrides.h"
#include "../ui/ui_theme.h"
#include "../ui/ui_chrome.h"
#include "../ui/ui_launch.h"
#include "../ui/ui_progress.h"

#include <libmcc/libmcc.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Fullscreen shell — replaces the floating-imgui-debug-windows look with a
// single root panel split into left rail (nav), main pane (content), right
// rail (network / sessions), and a bottom status strip. Inherits the global
// halox::ui theme so colors / sizing are consistent with the chrome.
//
// Each "page" reuses existing view bodies but skips their floating Begin/End:
//   - GAME   → c_imgui_game_mainmenu_view::render_internal() inline
//   - MULTI  → c_imgui_session_browser_view::render() inline (no begin/end)
//   - PLAYER → c_imgui_player_view::render() inline
//   - INPUT  → input pane (lifted from the old toggle window)
//   - SETUP  → MCC GameUserSettings.ini sliders (lifted from the old input pane)
//   - QUIT   → posts WM_CLOSE
//
// The old MainMenuBar with debug toggles is gone; press F4 + Shift to reach
// the demo window if you ever need it (see show_imgui_demo_combo below).

namespace {

enum e_shell_page {
	_page_game,
	_page_multiplayer,
	_page_player,
	_page_input,
	_page_settings,
	_page_count,
};

struct s_nav_item {
	const char* label;
	e_shell_page page;
};

constexpr s_nav_item k_nav[] = {
	{ "GAME",        _page_game },
	{ "MULTIPLAYER", _page_multiplayer },
	{ "PLAYER",      _page_player },
	{ "INPUT",       _page_input },
	{ "SETTINGS",    _page_settings },
};

constexpr float k_left_rail_w   = 220.0f;
constexpr float k_right_rail_w  = 360.0f;
constexpr float k_top_strip_h   = 42.0f;
constexpr float k_bot_strip_h   = 26.0f;
constexpr float k_nav_item_h    = 38.0f;

// Renders a single nav item: dim cyan when idle, brighter on hover, amber
// with corner brackets when selected. Returns true on click.
bool nav_item(const char* label, bool selected) {
	using namespace halox::ui;
	ImGui::PushID(label);
	ImVec2 p = ImGui::GetCursorScreenPos();
	ImVec2 size(ImGui::GetContentRegionAvail().x, k_nav_item_h);

	bool clicked = ImGui::InvisibleButton(label, size);
	bool hovered = ImGui::IsItemHovered();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec4 text_col;
	if (selected)      text_col = col_accent;
	else if (hovered)  text_col = col_accent_hi;
	else               text_col = col_text_dim;

	if (selected) {
		// Amber corner brackets framing the item.
		chrome_corner_brackets(dl,
			ImVec2(p.x + 4.0f, p.y + 4.0f),
			ImVec2(p.x + size.x - 4.0f, p.y + size.y - 4.0f),
			10.0f, 2.0f,
			ImGui::ColorConvertFloat4ToU32(col_accent));
	} else if (hovered) {
		// Thin cyan underline on hover.
		float y = p.y + size.y - 4.0f;
		dl->AddLine(
			ImVec2(p.x + 12.0f, y),
			ImVec2(p.x + size.x - 12.0f, y),
			ImGui::ColorConvertFloat4ToU32(col_accent_hi), 1.0f);
	}

	ImVec2 ts = ImGui::CalcTextSize(label);
	ImVec2 tp(p.x + 16.0f, p.y + (size.y - ts.y) * 0.5f);
	dl->AddText(tp, ImGui::ColorConvertFloat4ToU32(text_col), label);
	ImGui::PopID();
	return clicked;
}

// Page bodies -----------------------------------------------------------------

void page_game() {
	c_imgui_game_mainmenu_view().render_internal();
}

void page_multiplayer() {
	c_imgui_session_browser_view sv;
	sv.render();  // no begin/end — we're inside our own child region
}

void page_player() {
	c_imgui_player_view pv;
	pv.render();
}

void page_input() {
	float sensitivity = win32_input_get_mouse_sensitivity();
	if (ImGui::SliderFloat("Mouse sensitivity", &sensitivity, 0.0f, 2.0f, "%.3f")) {
		win32_input_set_mouse_sensitivity(sensitivity);
	}
	bool inv = win32_input_get_invert_y();
	if (ImGui::Checkbox("Invert Y", &inv)) {
		win32_input_set_invert_y(inv);
	}
	ImGui::TextDisabled(
		"Live multiplier on raw mouse counts sent to the game.\n"
		"Picking a Title applies that game's MCC sensitivity.");
}

// Coalesce TOML writes — dragging a slider produces dozens of edits per
// second; we'd churn %APPDATA% if we wrote on every changed frame. The
// pattern: any edit sets s_settings_dirty AND records the frame's time;
// page_settings flushes if dirty AND >500ms since the last edit (so the
// user has stopped moving) OR the page is about to be left.
static bool   s_settings_dirty       = false;
static double s_settings_dirty_until = 0.0;

static void settings_mark_dirty() {
	s_settings_dirty       = true;
	s_settings_dirty_until = ImGui::GetTime() + 0.5;  // 500ms quiescence
}

static void settings_maybe_flush() {
	if (s_settings_dirty && ImGui::GetTime() >= s_settings_dirty_until) {
		mcc_user_settings_save();
		s_settings_dirty = false;
	}
}

void page_settings() {
	using namespace libmcc;
	auto* s = mcc_user_settings_mut();
	if (!s->loaded) {
		ImGui::TextDisabled("(settings not loaded — TOML missing AND MCC ini missing)");
		return;
	}

	// Game selector. v1 wires only halo2; other games show the same widgets
	// but most won't take effect until per-game appliers exist (e.g. Wwise
	// audio for haloreach, h2a, etc.).
	static int s_module = (int)_module_halo2;
	static const char* names[k_game_count] = {
		"halo1", "halo2", "halo3", "halo4", "groundhog", "halo3odst", "haloreach"
	};
	ImGui::TextUnformatted("Game:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo("##halox_settings_game", names[s_module])) {
		for (int i = 0; i < k_game_count; ++i) {
			bool selected = (i == s_module);
			if (ImGui::Selectable(names[i], selected)) s_module = i;
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(saved to halox_settings.toml)");

	const auto module = (e_module)s_module;
	const bool h2 = (module == _module_halo2);

	ImGui::Separator();
	ImGui::TextUnformatted("Audio");
	{
		auto& a = s->audio[module];
		// Disable the slider for games whose audio backend isn't wired yet.
		const bool audio_supported = h2 || module == _module_halo1;
		if (!audio_supported) {
			ImGui::TextDisabled("(audio not yet wired for %s — Wwise RE pending)", names[module]);
		}
		ImGui::BeginDisabled(!audio_supported);
		if (ImGui::SliderInt("master##audio", &a.master_volume, 0, 100, "%d%%")) {
			settings_mark_dirty();
			halox::audio::apply_audio_for_module(module);
		}
		// SFX/music/voice are placeholders for now — Miles only exposes a
		// digital master. Bus-level controls need engine-side RE (find the
		// volume globals halo2 reads and hook them).
		ImGui::BeginDisabled(true);
		ImGui::SliderInt("sfx##audio",   &a.sfx_volume,   0, 100, "%d%%");
		ImGui::SliderInt("music##audio", &a.music_volume, 0, 100, "%d%%");
		ImGui::SliderInt("voice##audio", &a.voice_volume, 0, 100, "%d%%");
		ImGui::EndDisabled();
		if (ImGui::Checkbox("muted", &a.muted)) {
			settings_mark_dirty();
			halox::audio::apply_audio_for_module(module);
		}
		ImGui::EndDisabled();
	}

	ImGui::Separator();
	ImGui::TextUnformatted("View");
	if (ImGui::SliderInt("FOV##view",         &s->fov[module],         0, 120, s->fov[module] == 0 ? "default" : "%d")) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}
	if (ImGui::SliderInt("vehicle FOV##view", &s->vehicle_fov[module], 0, 120, s->vehicle_fov[module] == 0 ? "default" : "%d")) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Mouse");
	if (ImGui::SliderFloat("sensitivity##mouse", &s->mouse_sensitivity[module], 0.1f, 5.0f, "%.2f")) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}
	if (ImGui::Checkbox("invert Y##mouse", &s->mouse_look_inverted[module])) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}
	if (ImGui::Checkbox("smoothing##mouse", &s->mouse_smoothing[module])) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}
	if (ImGui::Checkbox("acceleration##mouse", &s->mouse_acceleration[module])) {
		settings_mark_dirty();
		apply_mcc_settings_for_module(module);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Profile toggles");
	if (ImGui::Checkbox("hold to zoom",         &s->hold_to_zoom[module]))         { settings_mark_dirty(); apply_mcc_settings_for_module(module); }
	if (ImGui::Checkbox("auto-center camera",   &s->auto_center_enabled[module]))  { settings_mark_dirty(); apply_mcc_settings_for_module(module); }
	if (ImGui::Checkbox("vibration disabled",   &s->vibration_disabled[module]))   { settings_mark_dirty(); apply_mcc_settings_for_module(module); }
	if (ImGui::Checkbox("modern aim control",   &s->use_modern_aim_control[module])){ settings_mark_dirty(); apply_mcc_settings_for_module(module); }
	if (ImGui::Checkbox("use elite model",      &s->use_elite_model[module]))      { settings_mark_dirty(); apply_mcc_settings_for_module(module); }

	ImGui::Separator();
	if (ImGui::Button("Save")) {
		mcc_user_settings_save();
		s_settings_dirty = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reload from disk")) {
		mcc_user_settings_reload();
		s_settings_dirty = false;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(also auto-saves ~0.5s after the last edit)");

	// Tail-coalesce save once the user stops dragging. Cheap if not dirty.
	settings_maybe_flush();
}

// Top strip ------------------------------------------------------------------

void render_top_strip(float w) {
	using namespace halox::ui;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 p = ImGui::GetCursorScreenPos();
	// Thin cyan rule under the wordmark.
	dl->AddLine(
		ImVec2(p.x, p.y + k_top_strip_h - 1.0f),
		ImVec2(p.x + w, p.y + k_top_strip_h - 1.0f),
		ImGui::ColorConvertFloat4ToU32(col_border), 1.0f);
	// Wordmark.
	ImVec2 t(p.x + 18.0f, p.y + 12.0f);
	dl->AddText(t, ImGui::ColorConvertFloat4ToU32(col_text), "HALOX");
	// Build/version on the right.
	const char* sub = "Halo MCC harness · build 0.1";
	ImVec2 ts = ImGui::CalcTextSize(sub);
	dl->AddText(ImVec2(p.x + w - ts.x - 18.0f, p.y + 12.0f),
		ImGui::ColorConvertFloat4ToU32(col_text_dim), sub);
	ImGui::Dummy(ImVec2(0, k_top_strip_h));
}

void render_bottom_strip(float win_w, float win_h) {
	using namespace halox::ui;
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	ImVec2 p(0.0f, win_h - k_bot_strip_h);
	// Thin amber rule on top of the strip.
	dl->AddLine(
		ImVec2(p.x, p.y),
		ImVec2(p.x + win_w, p.y),
		ImGui::ColorConvertFloat4ToU32(col_accent), 1.0f);
	// Status text.
	auto* gim = game_instance_manager();
	const char* state = gim->in_game() ? "in-game" :
		(halox::ui::g_launch_in_progress ? "launching" : "idle");
	char status[256];
	std::snprintf(status, sizeof(status),
		"state: %s   ·   F4: toggle UI   F11: borderless   ESC: pause   F3: cursor",
		state);
	dl->AddText(ImVec2(p.x + 18.0f, p.y + 6.0f),
		ImGui::ColorConvertFloat4ToU32(col_text_dim), status);
}

}  // namespace

// In-game pause overlay. Top-right HUD-style panel — no full-screen dim, the
// game frame stays fully visible. Sized so it overlays cleanly without
// hiding important gameplay area on the left/center of the screen.
enum e_pause_section {
	_pause_main,
	_pause_settings,
	_pause_bindings,
};

static void render_pause_overlay(float win_w, float win_h) {
	using namespace halox::ui;

	auto* gim = game_instance_manager();
	auto game = gim->get_game();
	auto mode = gim->get_mode();
	const bool show_campaign_actions =
		(game == libmcc::_module_halo2) && (mode == libmcc::_game_mode_campaign);

	static e_pause_section s_section = _pause_main;
	// Reset to main when the overlay opens fresh — track the rising edge
	// of g_show_imgui via a static so closing-and-reopening always lands
	// on the main button list.
	{
		static bool s_was_open = false;
		const bool open = g_show_imgui != 0;
		if (open && !s_was_open) s_section = _pause_main;
		s_was_open = open;
	}

	// Full-window dim backdrop — focuses attention on the pause panel.
	// (Real Gaussian blur would require a render-target round-trip; a
	// strong dark dim is the best we can do without that.)
	{
		ImDrawList* fg = ImGui::GetBackgroundDrawList();
		fg->AddRectFilled(ImVec2(0, 0), ImVec2(win_w, win_h),
			ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f)));
	}

	// Left-anchored panel — narrow when on the main section, expands
	// rightwards when a sub-page is open so settings/bindings have room.
	const bool wide = (s_section != _pause_main);
	const float panel_w = wide ? 720.0f : 420.0f;
	const float margin_y = 60.0f;
	const float panel_h = win_h - margin_y * 2.0f;
	const ImVec2 panel_min(0.0f, margin_y);
	const ImVec2 panel_max(panel_min.x + panel_w, panel_min.y + panel_h);

	// Solid panel fill + bracket accents.
	{
		ImDrawList* fg = ImGui::GetBackgroundDrawList();
		// Slightly stronger blur-like effect: nested rects from outer
		// faint to inner solid, to suggest depth.
		const ImU32 outer = ImGui::ColorConvertFloat4ToU32(
			ImVec4(col_bg.x, col_bg.y, col_bg.z, 0.85f));
		const ImU32 inner = ImGui::ColorConvertFloat4ToU32(
			ImVec4(col_bg.x, col_bg.y, col_bg.z, 0.97f));
		fg->AddRectFilled(panel_min, panel_max, outer);
		fg->AddRectFilled(
			ImVec2(panel_min.x, panel_min.y + 6.0f),
			ImVec2(panel_max.x - 6.0f, panel_max.y - 6.0f),
			inner);
		// Right edge cyan accent line.
		fg->AddLine(
			ImVec2(panel_max.x, panel_min.y + 6.0f),
			ImVec2(panel_max.x, panel_max.y - 6.0f),
			ImGui::ColorConvertFloat4ToU32(col_accent), 2.0f);
		chrome_corner_brackets(fg, panel_min, panel_max,
			22.0f, 2.0f,
			ImGui::ColorConvertFloat4ToU32(col_accent));
	}

	ImGui::SetNextWindowPos(panel_min);
	ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBackground |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##halox_pause", nullptr, flags)) {
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const float pad_x = 18.0f;

		// Header.
		ImGui::SetCursorScreenPos(ImVec2(panel_min.x + pad_x, panel_min.y + 14.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, col_accent);
		ImGui::TextUnformatted("PAUSED");
		ImGui::PopStyleColor();
		float rule_y = panel_min.y + 40.0f;
		dl->AddLine(
			ImVec2(panel_min.x + pad_x, rule_y),
			ImVec2(panel_max.x - pad_x, rule_y),
			ImGui::ColorConvertFloat4ToU32(col_accent));

		const char* names[] = {
			"halo1","halo2","halo3","halo4","groundhog","halo3odst","haloreach"
		};
		const char* gname = (game >= 0 && game < (int)(sizeof(names)/sizeof(names[0])))
			? names[game] : "?";
		ImGui::SetCursorScreenPos(ImVec2(panel_min.x + pad_x, panel_min.y + 52.0f));
		ImGui::TextDisabled("Title:");
		ImGui::SameLine();
		ImGui::TextUnformatted(gname);

		// Left button column — fixed width even when panel is wide.
		const float btn_col_w = 388.0f;
		const ImVec2 btn_size(btn_col_w - 4.0f, 38.0f);
		float btn_y = panel_min.y + 96.0f;

		auto place_btn = [&](float& y) {
			ImGui::SetCursorScreenPos(ImVec2(panel_min.x + pad_x, y));
			y += 46.0f;
		};

		place_btn(btn_y);
		if (ImGui::Button("RESUME", btn_size)) {
			game_instance_manager()->dismiss_pause_overlay();
		}

		if (show_campaign_actions) {
			place_btn(btn_y);
			if (ImGui::Button("REVERT TO LAST CHECKPOINT", btn_size)) {
				// Queue the revert flag, THEN resume — the per-tick pump
				// can only consume the flag while the engine is ticking.
				halo2_revert_to_checkpoint();
				game_instance_manager()->dismiss_pause_overlay();
			}

			place_btn(btn_y);
			if (ImGui::Button("RESTART MISSION", btn_size)) {
				// Same ordering as REVERT: flag first, then resume so the
				// pump processes it on its next tick.
				halo2_restart_mission();
				game_instance_manager()->dismiss_pause_overlay();
			}
		}

		place_btn(btn_y);
		// Highlight the active sub-section button.
		const bool sel_settings = s_section == _pause_settings;
		if (sel_settings) {
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.30f));
		}
		if (ImGui::Button("SETTINGS", btn_size)) {
			s_section = sel_settings ? _pause_main : _pause_settings;
		}
		if (sel_settings) ImGui::PopStyleColor();

		place_btn(btn_y);
		const bool sel_bindings = s_section == _pause_bindings;
		if (sel_bindings) {
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.30f));
		}
		if (ImGui::Button("BINDINGS", btn_size)) {
			s_section = sel_bindings ? _pause_main : _pause_bindings;
		}
		if (sel_bindings) ImGui::PopStyleColor();

		place_btn(btn_y);
		ImGui::PushStyleColor(ImGuiCol_Button,
			ImVec4(col_warning.x, col_warning.y, col_warning.z, 0.30f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(col_warning.x, col_warning.y, col_warning.z, 0.55f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			ImVec4(col_warning.x, col_warning.y, col_warning.z, 0.80f));
		if (ImGui::Button("QUIT TO MENU", btn_size)) {
			PostMessageW(g_win32_parameter.window_handle,
				_window_message_game_quit, 0, 0);
			InterlockedExchange(&g_show_imgui, FALSE);
		}
		ImGui::PopStyleColor(3);

		// Sub-page area to the right of the button column.
		if (wide) {
			const float sub_x = panel_min.x + pad_x + btn_col_w + 12.0f;
			const float sub_y = panel_min.y + 96.0f;
			const float sub_w = panel_max.x - sub_x - pad_x;
			const float sub_h = panel_max.y - sub_y - 40.0f;

			// Vertical separator.
			dl->AddLine(
				ImVec2(sub_x - 6.0f, sub_y),
				ImVec2(sub_x - 6.0f, sub_y + sub_h),
				ImGui::ColorConvertFloat4ToU32(col_border), 1.0f);

			ImGui::SetCursorScreenPos(ImVec2(sub_x, sub_y));
			// AlwaysVerticalScrollbar — page_settings is taller than the
			// pause panel can fit, so a scrollbar is mandatory; without it
			// the bottom controls (audio mute, save buttons, etc.) get
			// clipped off the panel and the user can't reach them.
			ImGui::BeginChild("##halox_pause_sub",
				ImVec2(sub_w, sub_h),
				ImGuiChildFlags_AlwaysUseWindowPadding,
				ImGuiWindowFlags_AlwaysVerticalScrollbar);
			if (s_section == _pause_settings) {
				chrome_section_header("SETTINGS");
				page_settings();
			} else if (s_section == _pause_bindings) {
				chrome_section_header("BINDINGS");
				page_input();
			}
			ImGui::EndChild();
		}

		// Hint footer.
		ImGui::SetCursorScreenPos(ImVec2(panel_min.x + pad_x, panel_max.y - 30.0f));
		ImGui::TextDisabled("ESC / F4 dismisses");
	}
	ImGui::End();
}

// Loading screen shown while a launch worker is running. Animated cyan ring
// at the center of the window plus the current launch step (which gets
// updated by ui_launch_set_status from inside launch_game_internal). This is
// the primary "is it alive?" feedback during game DLL init.
static void render_loading_screen(float win_w, float win_h) {
	using namespace halox::ui;

	// Background — full panel fill so the launcher menu doesn't peek through.
	{
		ImDrawList* fg = ImGui::GetBackgroundDrawList();
		fg->AddRectFilled(ImVec2(0, 0), ImVec2(win_w, win_h),
			ImGui::ColorConvertFloat4ToU32(col_bg));
		chrome_scanlines(fg, ImVec2(0, 0), ImVec2(win_w, win_h), 3);
		chrome_vignette(fg, ImVec2(0, 0), ImVec2(win_w, win_h), 0.55f);
		chrome_corner_brackets(fg,
			ImVec2(8.0f, 8.0f),
			ImVec2(win_w - 8.0f, win_h - 8.0f),
			28.0f, 1.5f,
			ImGui::ColorConvertFloat4ToU32(col_border));
	}

	const ImVec2 ring_center(win_w * 0.5f, win_h * 0.5f - 36.0f);
	const float radius = 56.0f;
	const float t = (float)ImGui::GetTime();

	ImDrawList* dl = ImGui::GetForegroundDrawList();

	// Sample real per-game loading progress (halo2: float %; reach: state).
	s_progress prog = halox::ui::progress_query();

	if (prog.valid && prog.fraction >= 0.0f) {
		// Halo2's progress float updates in coarse jumps (0.00 -> 0.35 ->
		// 0.60 -> ...), which makes the bar lurch. Ease the displayed
		// value toward the target so the arc and percentage animate
		// smoothly between samples. Also never go backwards if a stale
		// read briefly returns a smaller value.
		static float s_displayed = 0.0f;
		static double s_last_t = 0.0;
		static uint64_t s_seen_epoch = 0;
		// Detect a fresh launch and reset the eased value so the bar starts
		// at 0% on launch #2+ instead of inheriting 100% from the previous
		// load's terminal display.
		const uint64_t epoch = halox::ui::progress_launch_epoch();
		if (epoch != 0 && epoch != s_seen_epoch) {
			s_seen_epoch = epoch;
			s_displayed = 0.0f;
			s_last_t = 0.0;
		}
		const double t_now = ImGui::GetTime();
		float dt;
		if (s_last_t > 0.0) {
			double raw = t_now - s_last_t;
			if (raw < 0.0) raw = 0.0;
			if (raw > 0.1) raw = 0.1;
			dt = (float)raw;
		} else {
			dt = 1.0f / 60.0f;
		}
		s_last_t = t_now;
		const float target = prog.fraction;
		// During the finalize extension, ui_progress already produces a
		// perfectly smooth time-based ramp toward 1.0 — track it
		// precisely so the bar shows 100% at the exact frame
		// progress_is_loading() flips false (no easing lag).
		if (halox::ui::progress_finalize_started_ms() != 0) {
			s_displayed = target;
		} else {
			// Pre-stable: ease to target so smaller corrections look soft.
			const float diff = target - s_displayed;
			const float adiff = diff < 0.0f ? -diff : diff;
			const float speed = 1.5f + 4.0f * adiff;
			float step = dt * speed;
			if (s_displayed < target) {
				s_displayed += step;
				if (s_displayed > target) s_displayed = target;
			} else if (s_displayed > target + 0.01f) {
				s_displayed -= step * 0.5f;
				if (s_displayed < target) s_displayed = target;
			}
		}

		// Determinate progress arc — sweep from -90deg (top) clockwise by
		// `s_displayed` of the full circle. Background ring at low alpha for
		// the remaining arc.
		const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
			ImVec4(col_border.x, col_border.y, col_border.z, 0.25f));
		dl->AddCircle(ring_center, radius, bg, 96, 4.0f);

		const float two_pi = 6.28318530718f;
		const float start = -1.5707963f;  // top
		const int total_seg = 96;
		int filled_seg = (int)(total_seg * s_displayed + 0.5f);
		if (filled_seg < 1) filled_seg = 1;
		if (filled_seg > total_seg) filled_seg = total_seg;
		dl->PathClear();
		for (int i = 0; i <= filled_seg; ++i) {
			float a = start + ((float)i / (float)total_seg) * two_pi;
			dl->PathLineTo(ImVec2(ring_center.x + std::cos(a) * radius,
			                      ring_center.y + std::sin(a) * radius));
		}
		dl->PathStroke(ImGui::ColorConvertFloat4ToU32(col_accent), 0, 4.0f);

		// Big percentage in the center. Floor rather than round so "100%"
		// only displays at true 1.0 (otherwise the bar reads 100% for the
		// last ~150ms of the finalize ramp before the loading screen
		// actually dismisses, which feels like a stall).
		char pct[16];
		int pct_int = (int)(s_displayed * 100.0f);
		if (pct_int > 100) pct_int = 100;
		std::snprintf(pct, sizeof(pct), "%d%%", pct_int);
		ImVec2 sz = ImGui::CalcTextSize(pct);
		dl->AddText(ImVec2(ring_center.x - sz.x * 0.5f, ring_center.y - sz.y * 0.5f),
			ImGui::ColorConvertFloat4ToU32(col_text), pct);
	} else {
		// Indeterminate spinner — older games or pre-init phase.
		chrome_loading_ring(dl, ring_center, radius, t,
			ImGui::ColorConvertFloat4ToU32(col_accent));
		// Animated "..." dots in the center.
		int dot_count = 1 + ((int)(t * 2.5f) % 3);
		char dots[8] = {};
		for (int i = 0; i < dot_count; ++i) dots[i] = '.';
		ImVec2 sz = ImGui::CalcTextSize(dots);
		dl->AddText(ImVec2(ring_center.x - sz.x * 0.5f, ring_center.y - sz.y * 0.5f),
			ImGui::ColorConvertFloat4ToU32(col_text_dim), dots);
	}

	// Pulsing "LAUNCHING" wordmark above the ring.
	{
		const char* wordmark = prog.valid ? "LOADING" : "LAUNCHING";
		ImVec2 sz = ImGui::CalcTextSize(wordmark);
		float a = 0.55f + 0.45f * 0.5f * (1.0f + std::sin(t * 3.0f));
		ImU32 c = ImGui::ColorConvertFloat4ToU32(
			ImVec4(col_accent.x, col_accent.y, col_accent.z, a));
		dl->AddText(ImVec2(ring_center.x - sz.x * 0.5f, ring_center.y - radius - 36.0f),
			c, wordmark);
	}

	// Step text — prefer the in-game progress query (real step from the
	// game DLL), fall back to halox's own launch-status string.
	{
		const char* step = nullptr;
		char fallback[256] = {};
		if (prog.valid && prog.step[0]) {
			step = prog.step;
		} else {
			halox::ui::ui_launch_get_status(fallback, sizeof(fallback));
			if (fallback[0]) step = fallback;
		}
		if (step) {
			ImVec2 sz = ImGui::CalcTextSize(step);
			dl->AddText(ImVec2(ring_center.x - sz.x * 0.5f, ring_center.y + radius + 22.0f),
				ImGui::ColorConvertFloat4ToU32(col_text), step);
		}
	}

	// Hint footer.
	{
		const char* hint = "halox is loading the game DLL. Window will return on completion.";
		ImVec2 sz = ImGui::CalcTextSize(hint);
		dl->AddText(ImVec2((win_w - sz.x) * 0.5f, win_h - 40.0f),
			ImGui::ColorConvertFloat4ToU32(col_text_dim), hint);
	}
}

void c_imgui_main_view::render() {
	using namespace halox::ui;

	const float win_w = (float)g_win32_parameter.window_width;
	const float win_h = (float)g_win32_parameter.window_height;
	if (win_w < 100.0f || win_h < 100.0f) return;

	// Loading state — covers both the worker-launching phase and the long
	// in-game scenario-load phase that follows initialize_game (where the
	// game thread renders BLACK frames for many seconds while reading map
	// data). progress_is_loading() encodes the per-game heuristics.
	static bool s_was_loading = false;
	const bool launching = halox::ui::g_launch_in_progress;
	const bool loading_in_game = halox::ui::progress_is_loading();
	if (launching || loading_in_game) {
		s_was_loading = true;
		render_loading_screen(win_w, win_h);
		return;
	}

	// Loading just ended — auto-hide the imgui chrome on the SAME frame so
	// the game takes over the screen without requiring the user to press
	// ESC. Post the launch-finished message so cursor capture transfers
	// cleanly via the message-pump thread.
	if (s_was_loading && game_instance_manager()->in_game()) {
		s_was_loading = false;
		InterlockedExchange(&g_show_imgui, FALSE);
		PostMessageW(g_win32_parameter.window_handle,
			_window_message_game_launch_finished, 0, 0);
		return;
	}
	s_was_loading = false;

	// In-game: render only the corner pause overlay so the game frame
	// remains visible behind it. The full launcher shell is reserved for
	// when no game is running.
	if (game_instance_manager()->in_game()) {
		render_pause_overlay(win_w, win_h);
		return;
	}

	// Background pass — vignette + scanlines + thin cyan corner brackets framing
	// the whole window. Drawn on the foreground draw list of the root viewport
	// so nothing inside the window can obscure it.
	{
		ImDrawList* fg = ImGui::GetBackgroundDrawList();
		// Solid panel fill (so we don't see whatever was behind).
		fg->AddRectFilled(ImVec2(0, 0), ImVec2(win_w, win_h),
			ImGui::ColorConvertFloat4ToU32(col_bg));
		chrome_scanlines(fg, ImVec2(0, 0), ImVec2(win_w, win_h), 3);
		chrome_vignette(fg, ImVec2(0, 0), ImVec2(win_w, win_h), 0.55f);
		chrome_corner_brackets(fg,
			ImVec2(8.0f, 8.0f),
			ImVec2(win_w - 8.0f, win_h - 8.0f),
			28.0f, 1.5f,
			ImGui::ColorConvertFloat4ToU32(col_border));
	}

	// Single fullscreen window covering the entire client area, no chrome.
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(win_w, win_h));
	const ImGuiWindowFlags root_flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoBackground;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, col_bg_alt);
	if (ImGui::Begin("##halox_shell", nullptr, root_flags)) {
		render_top_strip(win_w);

		static e_shell_page s_page = _page_game;
		const float content_h = win_h - k_top_strip_h - k_bot_strip_h - 4.0f;

		// Left nav rail.
		ImGui::BeginChild("##nav", ImVec2(k_left_rail_w, content_h), true);
		for (auto& it : k_nav) {
			if (nav_item(it.label, s_page == it.page)) {
				s_page = it.page;
			}
		}
		// Quit button at the bottom of the rail.
		float remaining = ImGui::GetContentRegionAvail().y;
		if (remaining > k_nav_item_h + 8.0f) {
			ImGui::Dummy(ImVec2(0, remaining - k_nav_item_h - 4.0f));
		}
		ImGui::PushStyleColor(ImGuiCol_Text, col_warning);
		if (nav_item("QUIT", false)) {
			PostMessageW(g_win32_parameter.window_handle,
				_window_message_game_quit, 0, 0);
		}
		ImGui::PopStyleColor();
		ImGui::EndChild();
		ImGui::SameLine();

		// Main content pane.
		float main_w = win_w - k_left_rail_w - k_right_rail_w - 32.0f;
		if (main_w < 200.0f) main_w = 200.0f;
		ImGui::BeginChild("##content", ImVec2(main_w, content_h), true);
		switch (s_page) {
		case _page_game:        chrome_section_header("GAME");
			page_game(); break;
		case _page_multiplayer: chrome_section_header("MULTIPLAYER");
			page_multiplayer(); break;
		case _page_player:      chrome_section_header("PLAYER");
			page_player(); break;
		case _page_input:       chrome_section_header("INPUT");
			page_input(); break;
		case _page_settings:    chrome_section_header("SETTINGS");
			page_settings(); break;
		default: break;
		}
		ImGui::EndChild();
		ImGui::SameLine();

		// Right rail — sessions list, always visible.
		ImGui::BeginChild("##right", ImVec2(k_right_rail_w, content_h), true);
		chrome_section_header("NETWORK");
		c_imgui_session_browser_view sv;
		sv.render();
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleColor();

	render_bottom_strip(win_w, win_h);
}
