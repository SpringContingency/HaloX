#include "game_manager.h"

#include "../main/main.h"
#include "../logging/logging.h"
#include "game_instance_manager.h"

using namespace libmcc;

void __fastcall c_game_manager::set_game_state(e_game_state state) {
	CONSOLE_LOG_DEBUG("set_game_state:%d", state);
}

void __fastcall c_game_manager::restart_game(
	e_game_restart_reason reason, 
	const char* message
) {
	PostMessageW(
		g_win32_parameter.window_handle, 
		_window_message_game_restart, 
		reason, 
		reinterpret_cast<LPARAM>(message));
}

uintptr_t __fastcall libmcc::c_game_manager::update_launch_timer(
	int a2, 
	float a3
) {
	m_game_loading_progress = a3;

	return uintptr_t();
}

void __fastcall c_game_manager::save_game(const char* buf, uint32_t len) {
}

void __fastcall c_game_manager::set_game_result(s_game_result* result) {
}

void __fastcall c_game_manager::pause_game(int a1) {

}

void __fastcall c_game_manager::_pause_game_(int a1) {

}

void __fastcall c_game_manager::set_game_objectives(
	const wchar_t* primary, 
	const wchar_t* secondary) {

}

void __fastcall c_game_manager::set_game_engine_variant(i_game_engine_variant* variant) {
}

void __fastcall c_game_manager::set_scenario_map_variant(i_scenario_map_variant* variant) {
}

bool __fastcall c_game_manager::get_game_mode(
	int a2, 
	unsigned int a3, 
	wchar_t* buf, 
	size_t len
) {
	return false;
}

bool __fastcall c_game_manager::get_subtitle(
	const char* sound_tag_name, 
	const char* prefix, 
	int index, 
	wchar_t* buf, 
	size_t len
) {
	return false;
}
