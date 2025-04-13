#pragma once

#include <memory>
#include <libmcc/libmcc.h>

struct s_player_info {
	wchar_t name[32];
	libmcc::s_player_profile profile[libmcc::k_game_count];
	libmcc::s_gamepad_mapping mapping[libmcc::k_game_count];
};

class c_player_manager {
public:
	using XUID = libmcc::XUID;
	using e_module = libmcc::e_module;
	using e_local_player = libmcc::e_local_player;
	using s_player_profile = libmcc::s_player_profile;
	using s_gamepad_mapping = libmcc::s_gamepad_mapping;
	using s_game_specific_storage = libmcc::s_game_specific_storage;

	int initialize();
	int shutdown();

	int get_player_count();
	int set_player_count(int);

	int get_input_device(e_local_player);
	int set_input_device(e_local_player, int);

	XUID get_player_xuid(e_local_player);
	int set_player_xuid(e_local_player, XUID);

	int get_player_name(e_local_player, wchar_t [32]);
	int set_player_name(e_local_player, const wchar_t [32]);

	int get_player_profile(e_local_player, e_module, s_player_profile*);
	int set_player_profile(e_local_player, e_module, const s_player_profile*);

	int get_player_gamepad_mapping(e_local_player, e_module, s_gamepad_mapping*);
	int set_player_gamepad_mapping(e_local_player, e_module, const s_gamepad_mapping*);

	int set_player_look_control(e_local_player, e_module, bool);
	int set_game_specific(e_local_player, e_module, const s_game_specific_storage*);

	static c_player_manager g_player_manager;
private:
	volatile LONG m_player_count;
	volatile XUID m_player_xuid[libmcc::k_local_player_count];
	volatile LONG m_player_input_device[libmcc::k_local_player_count];

	SRWLOCK m_lock;
	std::unique_ptr<s_player_info[]> m_player_infos;
};

inline c_player_manager* player_manager() {
	return &c_player_manager::g_player_manager;
}
