#pragma once

#include <libmcc/libmcc.h>

class c_game_options : public libmcc::s_game_options {
public:
	c_game_options();
};

class c_campaign_game_options : public c_game_options {
public:
	c_campaign_game_options();
};

class c_spartan_ops_game_options : public c_game_options {
public:
	c_spartan_ops_game_options();
};

class c_multiplayer_game_options : public c_game_options {
public:
	c_multiplayer_game_options();
};

class c_ui_shell_game_options : public c_game_options {
public:
	c_ui_shell_game_options();
};

class c_firefight_game_options : public c_game_options {
public:
	c_firefight_game_options();
};

union s_game_options_storage {
	s_game_options_storage() {}

	c_game_options game_options;
	c_campaign_game_options campaign_game_options;
	c_spartan_ops_game_options spartan_ops_game_options;
	c_multiplayer_game_options multiplayer_game_options;
	c_ui_shell_game_options ui_shell_game_options;
	c_firefight_game_options firefight_game_options;
};
