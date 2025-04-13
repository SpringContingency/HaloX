	#include "game_manager.h"

#include "game_instance_manager.h"

#include "../main/main.h"

using namespace libmcc;

constexpr const char* k_folder_names[] = {
	"debug_logs",
	"config",
	"temporary",
	""
};

bool __fastcall c_game_manager::get_folder_path(
	e_folder folder, 
	wchar_t* buf, 
	size_t len
) {
	auto game = game_instance_manager()->get_game();

	auto game_name = k_game_names[game];

	if (!get_game_folder_path(folder, buf, len)) {
		return false;
	}

	auto result = swprintf_s(
		buf, 
		len, 
		L"%s\\%hs\\", 
		buf,
		game_name);

	return result != -1;
}

bool __fastcall c_game_manager::get_game_folder_path(
	e_folder folder, 
	wchar_t* buf, 
	size_t len
) {
	auto folder_name = k_folder_names[folder];

	auto result = swprintf_s(
		buf,
		len,
		L"%s\\%hs\\",
		main_get_root_folder(),
		folder_name);

	return result != -1;
}

bool __fastcall c_game_manager::get_scenario_path_a(
	bool builtin, 
	char* buf, 
	size_t len
) {
	DWORD length;
	size_t buf_len, tot_len;
	std::vector<char> buffer(len);

	length = GetFullPathNameA("./", buffer.size(), buffer.data(), NULL);

	if (length == 0) {
		return false;
	}

	buf_len = strlen(buffer.data());

	tot_len = buf_len + length;

	if (tot_len > len) {
		return false;
	}

	memcpy(buf + length, buf, sizeof(char) * buf_len);
	memcpy(buf, buffer.data(), sizeof(char) * length);

	buf[tot_len] = 0;

	return true;
}

bool __fastcall c_game_manager::get_scenario_path_w(
	bool builtin, 
	wchar_t* buf, 
	size_t len
) {
	DWORD length;
	size_t buf_len, tot_len;
	std::vector<wchar_t> buffer(len);

	length = GetFullPathNameW(L"./", buffer.size(), buffer.data(), NULL);

	if (length == 0) {
		return false;
	}

	buf_len = wcslen(buffer.data());

	tot_len = buf_len + length;

	if (tot_len > len) {
		return false;
	}

	memcpy(buf + length, buf, sizeof(wchar_t) * buf_len);
	memcpy(buf, buffer.data(), sizeof(wchar_t) * length);

	buf[tot_len] = 0;

	return true;
}

bool __fastcall c_game_manager::validate_cache_file(int a2) {
	return true;
}

bool __fastcall c_game_manager::get_map_info(s_scenario_map_id* id, void* a3, void* a4, void* a5) {
	return false;
}

bool __fastcall c_game_manager::get_campaign_map_info(const s_scenario_map_id* id, const char** info, uint32_t* size) {
	return false;
}

bool __fastcall c_game_manager::get_multiplayer_map_info(const s_scenario_map_id* id, const char** info, uint32_t* size) {
	return false;
}

bool __fastcall c_game_manager::get_ugc_id(s_scenario_map_id* map_id, char* buf, size_t len) {
	return false;
}
