#include "player_manager.h"

#include <Windows.h>
#include <coguid.h>

c_player_manager c_player_manager::g_player_manager;

using namespace libmcc;

int c_player_manager::initialize() {
	union {
		GUID guid;
		libmcc::XUID xuid;
		struct {
			uint64_t low;
			uint64_t high;
		};
	} id;

	id.xuid = FAILED(CoCreateGuid(&id.guid)) ? 0x10000000 : id.low ^ id.high;

	m_lock = SRWLOCK_INIT;
	m_player_count = 1;
    m_player_infos = std::make_unique<s_player_info[]>(k_local_player_count);

	for (int i = 0; i < k_local_player_count; ++i) {
		auto info = m_player_infos.get() + i;

		m_player_xuid[i] = id.xuid + i;

		for (int j = 0; j < k_game_count; ++j) {
			auto profile = info->profile + j;

			profile->master_volume = 1.0f;
			profile->music_volume = 1.0f;
			profile->sfx_volume = 1.0f;
		}
	}

	return 0;
}

int c_player_manager::shutdown() {
	return 0;
}

int c_player_manager::get_player_count() {
	return m_player_count;
}

int c_player_manager::set_player_count(int count) {
	return InterlockedExchange(&m_player_count, count);
}

int c_player_manager::get_input_device(e_local_player player) {
	return m_player_input_device[player];
}

int c_player_manager::set_input_device(e_local_player player, int input_device) {
	return InterlockedExchange(m_player_input_device + player, input_device);
}

XUID c_player_manager::get_player_xuid(e_local_player player) {
	return m_player_xuid[player];
}

int c_player_manager::set_player_xuid(e_local_player player, XUID xuid) {
	auto p_xuid = reinterpret_cast<volatile long long*>(m_player_xuid + player);
	return InterlockedExchange64(p_xuid, xuid);
}

int c_player_manager::get_player_name(e_local_player player, wchar_t name[32]) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockShared(&m_lock);
	memcpy(name, info->name, sizeof(info->name));
	ReleaseSRWLockShared(&m_lock);
	return 0;
}

int c_player_manager::set_player_name(e_local_player player, const wchar_t name[32]) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockExclusive(&m_lock);
	memcpy(info->name, name, sizeof(info->name));
	ReleaseSRWLockExclusive(&m_lock);
	return 0;
}

int c_player_manager::get_player_profile(
	e_local_player player, 
	e_module game, 
	s_player_profile* profile
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockShared(&m_lock);
	memcpy(profile, info->profile + game, sizeof(*profile));
	ReleaseSRWLockShared(&m_lock);
	return 0;
}

int c_player_manager::set_player_profile(
	e_local_player player, 
	e_module game, 
	const s_player_profile* profile
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockExclusive(&m_lock);
	memcpy(info->profile, profile, sizeof(*profile));
	ReleaseSRWLockExclusive(&m_lock);
	return 0;
}

int c_player_manager::get_player_gamepad_mapping(
	e_local_player player, 
	e_module game, 
	s_gamepad_mapping* mapping
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockShared(&m_lock);
	memcpy(mapping, info->mapping + game, sizeof(*mapping));
	ReleaseSRWLockShared(&m_lock);
	return 0;
}

int c_player_manager::set_player_gamepad_mapping(
	e_local_player player, 
	e_module game, 
	const s_gamepad_mapping* mapping
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockExclusive(&m_lock);
	memcpy(info->mapping + game, mapping, sizeof(*mapping));
	ReleaseSRWLockExclusive(&m_lock);
	return 0;
}

int c_player_manager::set_player_look_control(
	e_local_player player, 
	e_module game, 
	bool inverted
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockExclusive(&m_lock);
	info->profile[game].look_controls_inverted = inverted;
	ReleaseSRWLockExclusive(&m_lock);
	return 0;
}

int c_player_manager::set_game_specific(
	e_local_player player, 
	e_module game, 
	const s_game_specific_storage* game_specific
) {
	auto info = m_player_infos.get() + player;
	AcquireSRWLockExclusive(&m_lock);
	memcpy(&info->profile[game].game_specific, game_specific, sizeof(*game_specific));
	ReleaseSRWLockExclusive(&m_lock);
	return 0;
}
