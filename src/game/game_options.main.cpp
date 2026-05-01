#include "game_options.h"

using namespace libmcc;

c_game_options::c_game_options() {
	memset(this, 0, sizeof(*this));
#ifdef _DEBUG
	flags.bit_set(_game_options_flags_debug, true);
#endif 
	game_tick = 60;

	flags.bit_set(_game_options_flags_multiplayer, true);
}

c_campaign_game_options::c_campaign_game_options() : c_game_options() {
	game_mode = _game_mode_campaign;
	// MP-flag clear is REACH-SPECIFIC — performed in launch.cpp's campaign
	// branch only when prop->module == _module_haloreach. Halo2 campaign
	// freezes at set_game_state:0 if the flag is cleared (engine takes a
	// path that never advances), so we leave the parent's bit untouched here.
}

c_spartan_ops_game_options::c_spartan_ops_game_options() : c_game_options() {
	game_mode = _game_mode_spartan_ops;
}

c_multiplayer_game_options::c_multiplayer_game_options() : c_game_options() {
	game_mode = _game_mode_multiplayer;
}

c_ui_shell_game_options::c_ui_shell_game_options() : c_game_options() {
	game_mode = _game_mode_ui_shell;
}

c_firefight_game_options::c_firefight_game_options() : c_game_options() {
	game_mode = _game_mode_firefight;
}
