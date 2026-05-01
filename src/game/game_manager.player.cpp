#include "game_manager.h"

#include "../player/player_manager.h"
#include "../game/game_instance_manager.h"
#include "../game/mcc_user_settings.h"

using namespace libmcc;

static s_player_profile g_player_profile;

ID3D11ShaderResourceView* __fastcall c_game_manager::get_player_skin(uint32_t a1, uint32_t a2) {
	return nullptr;
}

ID3D11ShaderResourceView* __fastcall c_game_manager::get_player_emblem(XUID xuid) {
	return nullptr;
}
void __fastcall c_game_manager::get_player_emblem_attribute(XUID xuid, uint32_t* a2, uint32_t* a3, uint32_t* a4) {

}

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

		auto module = game_instance_manager()->get_game();
		player_manager()->get_player_profile(
			player,
			module,
			&profile
		);

		// Overlay the latest MCC settings (including any live imgui edits) on
		// top of the profile that player_manager just handed us. Doing this on
		// every fetch means the next frame the game queries the profile, the
		// user's slider edits are visible to the title without us having to
		// hook a "settings changed" event.
		mcc_user_settings_stamp_profile(module, &profile);

		return &profile;
	}

	return nullptr;
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

