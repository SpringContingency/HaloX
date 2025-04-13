#include "game_instance_manager.h"

using namespace libmcc;

int c_game_instance_manager::load_local() {
	HANDLE handle;
	char buffer[1024];
	WIN32_FIND_DATAA find_data;

	for (int i = _module_halo1; i < k_game_count; ++i) {
		auto local = s_game_locals + i;

		local->hopper_game_variants.clear();
		local->hopper_map_variants.clear();

		if (!m_game_module_status.bit_test(i)) {
			continue;
		}

		auto title = k_game_names[i];

		sprintf(buffer, "./%s/hopper_map_variants/*.mvar", title);

		if ((handle = FindFirstFileA(buffer, &find_data)) != INVALID_HANDLE_VALUE) {
			do {
				local->hopper_map_variants.emplace_back(find_data.cFileName);
			} while (FindNextFileA(handle, &find_data));
			FindClose(handle);
		}

		sprintf(buffer, "./%s/hopper_game_variants/*.bin", title);

		if ((handle = FindFirstFileA(buffer, &find_data)) != INVALID_HANDLE_VALUE) {
			do {
				local->hopper_game_variants.emplace_back(find_data.cFileName);
			} while (FindNextFileA(handle, &find_data));
			FindClose(handle);
		}
	}

	return 0;
}

i_unknown_ptr<i_unknown> c_game_instance_manager::get_saved_game_file(
	e_module game, 
	int game_file_type, 
	int game_file_index
) {
	int status;
	HANDLE handle;
	i_unknown* result;
	wchar_t buffer[1024];
	auto local = s_game_locals + game;
	auto variants = game_file_type ? &local->hopper_map_variants : &local->hopper_game_variants;

	if (game_file_index < 0 || game_file_index >= variants->size()) {
		return nullptr;
	}

	auto data_access = get_module_data_access(game);

	if (!data_access) {
		return nullptr;
	}

	auto name = variants->at(game_file_index).c_str();

	swprintf(
		buffer, 
		L"./%hs/%s/%hs", 
		k_game_names[game],
		game_file_type ? L"hopper_map_variants" : L"hopper_game_variants",
		name);

	handle = CreateFileW(
		buffer,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) {
		return nullptr;
	}

	auto file_size = GetFileSize(handle, nullptr);

	if (file_size == INVALID_FILE_SIZE) {
		CloseHandle(handle);
		return nullptr;
	}

	std::vector<char> file_data(file_size);

	status = ReadFile(
		handle,
		file_data.data(),
		file_size,
		nullptr,
		nullptr
	);

	if (status == 0) {
		CloseHandle(handle);
		return nullptr;
	}

	CloseHandle(handle);

	if (game_file_type) {
		result = data_access->create_scenario_map_variant_from_file(
			file_data.data(),
			file_size
		);
	} else {
		result = data_access->create_game_engine_variant_from_file(
			file_data.data(),
			file_size
		);
	}

	return i_unknown_ptr<i_unknown>(result);
}

i_unknown_ptr<i_game_engine_variant> c_game_instance_manager::get_game_variant(
	e_module game, 
	int hopper_game_variant
) {
	auto result = get_saved_game_file(game, 0, hopper_game_variant).release();
	return i_unknown_ptr<i_game_engine_variant>(reinterpret_cast<i_game_engine_variant*>(result));
}

i_unknown_ptr<i_scenario_map_variant> c_game_instance_manager::get_map_variant(
	e_module game, 
	int hopper_map_variant
) {
	auto result = get_saved_game_file(game, 1, hopper_map_variant).release();
	return i_unknown_ptr<i_scenario_map_variant>(reinterpret_cast<i_scenario_map_variant*>(result));
}
