#include "game_instance_manager.h"

using namespace libmcc;

c_game_instance_manager c_game_instance_manager::g_game_instance_manager;

int c_game_instance_manager::initialize() {
	initialize_module();
	load_local();

	return 0;
}

int c_game_instance_manager::shutdown() {
	shutdown_module();

	return 0;
}

int c_game_instance_manager::post_message(
	e_game_message message,
	const s_game_message_parameter* parameter
) {
	if (!m_game_thread || !m_game_engine) {
		return 1;
	}

	m_game_engine->post_message(message, parameter);

	return 0;
}
