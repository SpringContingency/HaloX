#pragma once

#include "imgui_view.h"

#include <libmcc/libmcc.h>

// Programmatic launch entry point. Parses a `--launch=<spec>` argument and
// stamps the result into the launcher's prop struct, scheduling the launch
// to fire on the next frame (so all subsystems are initialized first).
//
// `spec` format (colon-separated, fields after the third are optional):
//   <module>:<mode>:<map>[:<difficulty>[:<variant_index>]]
//
// Examples:
//   halo3:campaign:sierra_117:normal
//   halo3:multiplayer:guardian:0:5
//   haloreach:firefight:beachhead
//
// Module names: halo1, halo2, halo3, halo4, halo2a/groundhog, halo3odst,
//   haloreach. Mode names: campaign, multiplayer, mp, firefight, ff,
//   spartan_ops, ui_shell. Map: case-insensitive substring of the display
//   name from k_map_entries (scoped to the chosen module). Difficulty:
//   easy, normal, hard, impossible.
//
// Returns true if the spec parsed AND a matching map was found. The actual
// launch is deferred — call game_view_consume_pending_launch() once per
// frame from the main loop to pick it up.
bool game_view_request_launch_from_spec(const char* spec);

// Pulled from the main loop right before the menu would render, to honor
// any pending --launch request. Returns 0 on success, non-zero is the
// launch_game error code, or -999 if no request was pending.
int game_view_consume_pending_launch();

class c_imgui_game_view : public c_imgui_view {
public:
	void render() override;
protected:
	virtual void render_internal() = 0;
};

class c_imgui_game_mainmenu_view : public c_imgui_game_view {
public:
	// Public so the shell view can embed the launcher controls inline,
	// without spawning the floating Begin("Game Window") wrapper.
	void render_internal() override;
private:
	void render_campaign();
	void render_multiplayer();
};

class c_imgui_game_ingame_view : public c_imgui_game_view {
public:
	// Override render() so the in-game surface fills the entire halox
	// window (no chrome, no padding) — the pause overlay is drawn on top
	// of this by c_imgui_main_view.
	void render() override;
private:
	void render_internal() override;
};
