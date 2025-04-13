#include "game_instance_manager.h"

using namespace libmcc;

int c_game_instance_manager::initialize_module() {
	load_modules(-1);

	return 0;
}

int c_game_instance_manager::shutdown_module() {
	unload_modules(-1);

	return 0;
}

s_module_flags c_game_instance_manager::load_modules(s_module_flags flags) {
	wchar_t buffer[1024];

	for (int i = 0; i < k_game_count; i++) {
		auto info = m_game_module_infos + i;

		if (!flags.bit_test(i)) {
			continue;
		}

		if (info->module_handle!= nullptr) {
			continue;
		}

		auto module_name = k_game_names[i];

		swprintf(buffer, L"./%hs/%hs.dll", module_name, module_name);

		auto hModule = LoadLibraryW(buffer);

		if (!hModule) {
			info->error_code = GetLastError();
			continue;
		}

		info->module_handle = hModule;

		switch (i) {
		case _module_halo3: {
			halo3::hModule = hModule;
		}
		default: 
			break;
		}

		info->create_data_access = reinterpret_cast<t_expf_create_data_access>(
			GetProcAddress(info->module_handle, EXPORT_FUNCTION_CREATE_DATA_ACCESS)
			);

		if (!info->create_data_access) {
			info->error_code = GetLastError();
			FreeLibrary(info->module_handle);
			info->module_handle = nullptr;
			continue;
		}

		info->create_data_access(&info->data_access);
		info->error_code = 0;
		m_game_module_status.bit_set(i, true);
	}

	return m_game_module_status;
}

s_module_flags c_game_instance_manager::unload_modules(s_module_flags flags) {
	for (int i = 0; i < k_game_count; i++) {
		auto info = m_game_module_infos + i;

		if (!flags.bit_test(i)) {
			continue;
		}

		if (info->module_handle == nullptr) {
			continue;
		}

		if (info->data_access != nullptr) {
			info->data_access->free();
			info->data_access = nullptr;
		}

		if (!FreeLibrary(info->module_handle)) {
			info->error_code = GetLastError();
			continue;
		}

		memset(info, 0, sizeof(*info));
		m_game_module_status.bit_set(i, false);
	}

	return m_game_module_status;
}

s_module_flags c_game_instance_manager::get_module_states() {
	return m_game_module_status;
}

i_data_access* c_game_instance_manager::get_module_data_access(e_module module) {
	auto info = m_game_module_infos + module;

	if (info->module_handle == nullptr ||
		info->create_data_access == nullptr ||
		info->data_access == nullptr) {
		return nullptr;
	}

	return info->data_access;
}

