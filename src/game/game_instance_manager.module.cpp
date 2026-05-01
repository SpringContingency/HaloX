#include "game_instance_manager.h"

#include "../logging/logging.h"

#include <string>

extern "C" const wchar_t* halox_resolve_mcc_root();

using namespace libmcc;

int c_game_instance_manager::initialize_module() {
	// AddDllDirectory'd entries are only consulted when the loader's default
	// search policy includes LOAD_LIBRARY_SEARCH_USER_DIRS. Switch the process
	// to the safe modern default here so the per-game AddDllDirectory calls
	// below actually take effect for delay-load resolution.
	if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
		CONSOLE_LOG_WARN("SetDefaultDllDirectories failed: %lu", GetLastError());
	}

	// Set cwd to the MCC content root before loading any DLL. Game DLLs do
	// cwd-relative reads to find their own content (maps, fonts, ranks); for
	// halo3 this used to work via a manual `./halo3` junction next to the
	// exe, but Reach (and the other games) didn't have equivalent junctions
	// and would silently exit when their content reads failed. Pointing cwd
	// at MCC root makes `./<game>/...` resolve for every game without
	// junctions.
	if (auto mcc_root = halox_resolve_mcc_root()) {
		if (SetCurrentDirectoryW(mcc_root)) {
			CONSOLE_LOG_INFO("cwd set to MCC content root: %ls", mcc_root);
		} else {
			CONSOLE_LOG_WARN("SetCurrentDirectoryW(%ls) failed: %lu",
				mcc_root, GetLastError());
		}

		// Add the MCC root itself to the process DLL search path. Static and
		// delay-loaded imports of the game DLLs can land in MCC root in some
		// installs (shared helper DLLs).
		if (AddDllDirectory(mcc_root) != nullptr) {
			CONSOLE_LOG_INFO("AddDllDirectory(MCC root) ok: %ls", mcc_root);
		} else {
			CONSOLE_LOG_WARN("AddDllDirectory(%ls) failed: %lu",
				mcc_root, GetLastError());
		}

		// Add each per-game folder. LoadLibraryExW(LOAD_WITH_ALTERED_SEARCH_PATH)
		// covers static imports of the game DLL itself, but delay-loaded imports
		// resolved later by __delayLoadHelper2 use the *default* process search
		// order, so we have to register each game folder explicitly. Without
		// this, halo2.dll's delay-loaded helpers raise 0xC06D007E mid-game.
		for (int i = 0; i < k_game_count; i++) {
			auto module_name = k_game_names[i];
			wchar_t wname[64];
			MultiByteToWideChar(CP_UTF8, 0, module_name, -1, wname, 64);
			std::wstring game_dir = std::wstring(mcc_root) + L"\\" + wname;
			if (AddDllDirectory(game_dir.c_str()) != nullptr) {
				CONSOLE_LOG_INFO("AddDllDirectory(%s) ok: %ls",
					module_name, game_dir.c_str());
			} else {
				CONSOLE_LOG_WARN("AddDllDirectory(%ls) failed: %lu",
					game_dir.c_str(), GetLastError());
			}
		}
	}

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

		// Try cwd-relative first (junctioned setups), then fall back to the
		// MCC content root. Using SetDllDirectoryW so that haloreach.dll can
		// still find its sibling DLLs (e.g. WavesLibDLL.dll for halo3) when
		// loaded by absolute path.
		swprintf(buffer, L"./%hs/%hs.dll", module_name, module_name);
		HMODULE hModule = LoadLibraryW(buffer);

		if (!hModule) {
			DWORD cwd_err = GetLastError();
			const wchar_t* mcc_root = halox_resolve_mcc_root();
			if (mcc_root) {
				std::wstring abs_dll = std::wstring(mcc_root) + L"\\";
				wchar_t wname[64];
				MultiByteToWideChar(CP_UTF8, 0, module_name, -1, wname, 64);
				abs_dll += wname; abs_dll += L"\\"; abs_dll += wname; abs_dll += L".dll";

				// Side-load with the DLL's directory on the search path so any
				// sibling DLLs the game wants (e.g. WavesLibDLL.dll) resolve.
				std::wstring dll_dir = std::wstring(mcc_root) + L"\\" + wname;
				hModule = LoadLibraryExW(
					abs_dll.c_str(), nullptr,
					LOAD_WITH_ALTERED_SEARCH_PATH);
				if (hModule) {
					CONSOLE_LOG_INFO(
						"Loaded %s.dll from MCC root: %ls",
						module_name, abs_dll.c_str());
				} else {
					info->error_code = GetLastError();
					CONSOLE_LOG_WARN(
						"%s.dll load failed: cwd-relative err=%lu, MCC-root err=%lu (%ls)",
						module_name, cwd_err, info->error_code, abs_dll.c_str());
					continue;
				}
			} else {
				info->error_code = cwd_err;
				CONSOLE_LOG_WARN(
					"LoadLibraryW(\"./%s/%s.dll\") failed: GetLastError=%lu and no MCC root resolved. "
					"Set HALOX_MCC_ROOT or junction %s\\ next to halox.exe.",
					module_name, module_name, cwd_err, module_name);
				continue;
			}
		} else {
			CONSOLE_LOG_INFO("Loaded %s.dll @ 0x%p", module_name, hModule);
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
			CONSOLE_LOG_WARN(
				"%s.dll loaded but missing %s export (GetLastError=%lu)",
				module_name, EXPORT_FUNCTION_CREATE_DATA_ACCESS, info->error_code);
			FreeLibrary(info->module_handle);
			info->module_handle = nullptr;
			continue;
		}

		info->create_data_access(&info->data_access);
		CONSOLE_LOG_INFO(
			"%s data_access=0x%p", module_name, info->data_access);
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

