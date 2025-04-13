#include "game_manager.h"

#include "../player/player_manager.h"
#include "../game/game_instance_manager.h"

using namespace libmcc;

static s_player_profile g_player_profile;

void __fastcall c_game_manager::set_player_look_control(
	e_local_player player, 
	bool inverted
) {
	player_manager()->set_player_look_control(
		player,
		game_instance_manager()->get_game(),
		inverted);
}

void __fastcall c_game_manager::set_player_profile_game_specific(
	e_local_player player, 
	const s_game_specific_storage* game_specific
) {
	//!crash
	player_manager()->set_game_specific(
		player,
		game_instance_manager()->get_game(),
		game_specific);
}

bool __fastcall c_game_manager::get_player_xuid(XUID* xuid, wchar_t* name, int size, e_local_player player) {
	wchar_t buffer[0x20];

	if (player < 0 || player >= player_manager()->get_player_count()) {
		return false;
	}

	if (xuid) {
		*xuid = player_manager()->get_player_xuid(player);
	}

	if (name) {
		if (size > 0) {
			player_manager()->get_player_name(player, buffer);
			wcsncpy(name, buffer, size / 2);
			name[size / 2 - 1] = 0;
		}
	}

	return true;
}

s_gamepad_mapping* __fastcall c_game_manager::get_player_gamepad_mapping(XUID xuid) {
	static s_gamepad_mapping mapping;

	for (int i = 0; i < player_manager()->get_player_count(); ++i) {
		auto player = static_cast<e_local_player>(i);

		if (player_manager()->get_player_xuid(player) != xuid) {
			continue;
		}

		player_manager()->get_player_gamepad_mapping(
			player,
			game_instance_manager()->get_game(),
			&mapping
		);

		return &mapping;
	}

	return nullptr;
}

s_player_profile* __fastcall c_game_manager::get_player_profile(XUID xuid) {
	static s_player_profile profile;

	for (int i = 0; i < player_manager()->get_player_count(); ++i) {
		auto player = static_cast<e_local_player>(i);

		if (player_manager()->get_player_xuid(player) != xuid) {
			continue;
		}

		player_manager()->get_player_profile(
			player,
			game_instance_manager()->get_game(),
			&profile
		);

		return &profile;
	}

	return nullptr;
}

void __fastcall c_game_manager::draw_player_icon(
	XUID xuid, 
	int, 
	float x0, 
	float x1, 
	float y0, 
	float y1
) {
}

bool __fastcall c_game_manager::get_player_weapon_offset(
	datum_index index, 
	e_local_player player, 
	const char* tag_name, 
	real_vector3d* offset, 
	bool dual_wielding
) {
	return false;
}

