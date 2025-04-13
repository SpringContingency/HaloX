#include "game_instance_manager.h"

#include "../logging/logging.h"
#include "../player/player_manager.h"
#include "../rasterizer/rasterizer.h"

using namespace libmcc;

int c_game_instance_manager::launch_game_internal() {
	FARPROC func;
	t_expf_create_game_engine create_game_engine;
	t_expf_set_library_settings set_library_settings;
	s_language_settings language_settings;

	auto info = m_game_module_infos + m_game;
	auto device = rasterizer()->get_device();
	auto context = rasterizer()->get_device_context();
	auto swapchain = rasterizer()->get_swap_chain();

	if (m_game_thread) {
		return 1;
	}

	if (info->module_handle == nullptr) {
		return -1;
	}

	// set language settings
	func = GetProcAddress(info->module_handle, EXPORT_FUNCTION_SET_LIBRARY_SETTINGS);

	if (func == nullptr) {
		return -2;
	}

	set_library_settings = reinterpret_cast<t_expf_set_library_settings>(func);

	wcscpy(language_settings.audio, k_languages[_language_en_US]);
	wcscpy(language_settings.text_1, k_languages[_language_en_US]);
	wcscpy(language_settings.text_2, k_languages[_language_en_US]);

	if (set_library_settings(&language_settings)) {
		return -3;
	}

	// create game engine
	func = GetProcAddress(info->module_handle, EXPORT_FUNCTION_CREATE_GAME_ENGINE);

	if (func == nullptr) {
		return -4;
	}

	create_game_engine = reinterpret_cast<t_expf_create_game_engine>(func);

	create_game_engine(&m_game_engine);

	if (m_game_engine == nullptr) {
		return -5;
	}

	// initialize game engine
	m_game_engine->initialize(device, context, swapchain, nullptr);

	m_game_thread = m_game_engine->initialize_game(
		&g_game_manager, 
		&m_game_options_storage.game_options);

	if (m_game_thread == nullptr) {
		return GetLastError();
	}

	CONSOLE_LOG_DEBUG("Game Thread: %llX\n", m_game_thread);

	return 0;
}

int c_game_instance_manager::launch_game(const s_game_prop* prop) {
	m_game = prop->module;

	auto info = m_game_module_infos + m_game;
	auto game_options = &m_game_options_storage.game_options;
	i_unknown_ptr<i_scenario_map_variant> map_variant = nullptr;
	i_unknown_ptr<i_game_engine_variant> game_variant = nullptr;

	auto data_access = get_module_data_access(prop->module);

	if (data_access == nullptr) {
		return -1;
	}

	switch (prop->mode) {
	case _game_mode_campaign:
		if (prop->mode == _module_groundhog) {
			return -2;
		}
		new (&m_game_options_storage.campaign_game_options) c_campaign_game_options();
		game_options->difficulty_level = prop->difficulty;
		break;
	case _game_mode_spartan_ops:
		if (prop->mode != _module_halo4) {
			return -2;
		}
		new (&m_game_options_storage.spartan_ops_game_options) c_spartan_ops_game_options();
		break;
	case _game_mode_multiplayer:
		if (prop->mode == _module_halo3odst) {
			return -2;
		}
		new (&m_game_options_storage.multiplayer_game_options) c_multiplayer_game_options();

		if (prop->hopper_game_variant == -1) {
			return -2;
		} else {
			game_variant = get_game_variant(prop->module, prop->hopper_game_variant);

			if (game_variant == nullptr) {
				return -2;
			}
		}

		if (prop->module == _module_halo1 || 
			prop->module == _module_halo2 || 
			prop->module == _module_halo3odst
		) {
			map_variant = nullptr;
		} else {
#ifdef _DEBUG
			map_variant = 
				i_unknown_ptr<i_scenario_map_variant>(data_access->create_scenario_map_variant_from_map_id(&prop->map));
#else
			if (hopper_map_variant == -1) {
				return -2;
			}


			map_variant = get_map_variant(game, hopper_map_variant);
#endif


			if (map_variant == nullptr) {
				return -2;
			}
		}

		if (game_variant) {
			game_variant->copy_to_game_options(game_options);
		}

		if (map_variant) {
			map_variant->copy_to_game_options(game_options);
		}

		break;
	case _game_mode_ui_shell:
		new (&m_game_options_storage.ui_shell_game_options) c_ui_shell_game_options();
		break;
	case _game_mode_firefight:
		if (!(prop->module == _module_halo3odst || prop->module == _module_haloreach)) {
			return -2;
		}
		new (&m_game_options_storage.firefight_game_options) c_firefight_game_options();
		break;
	default:
		return -2;
	}

	// set map id
	game_options->map_id = prop->map;

	if (game_options->map_id.is_builtin()) {
		game_options->legacy_map_id = game_options->map_id.builtin_map_id;
	}

	game_options->host_address = 123;

	// set player options
	game_options->player_options.player_count = 1;
	game_options->player_options.peer_count = 1;

	game_options->player_options.address[0] = game_options->host_address;
	game_options->player_options.player_options[0].xuid = player_manager()->get_player_xuid(_local_player_0);
	game_options->player_options.player_options[0].address = game_options->host_address;

	if (!PostMessageW(g_win32_parameter.window_handle, _window_message_game_launch, 0, 0)) {
		return GetLastError();
	}

	return 0;
}
