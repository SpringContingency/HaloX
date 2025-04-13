#include "game_manager.h"

using namespace libmcc;

bool __fastcall c_game_manager::font__() {
	return false;
}

bool __fastcall c_game_manager::font___() {
	return false;
}

bool __fastcall c_game_manager::font_test_string(
	const wchar_t* str, 
	int font_id, 
	float scale, 
	const char* font_name
) {
	return false;
}

bool __fastcall c_game_manager::font_precache_character(
	wchar_t c, 
	s_font_character* character, 
	int font_id, 
	float scale, 
	const char* font_name
) {
	return false;
}

bool __fastcall c_game_manager::font_test__(int) {
	return false;
}

bool __fastcall c_game_manager::font_test_char(
	wchar_t c, 
	int font_id, 
	float scale, 
	const char* font_name
) {
	return false;
}

int __fastcall c_game_manager::font_get_kerning_pair_offset(
	wchar_t c, 
	int, 
	int font_id, 
	float scale, 
	const char* font_name
) {
	return 0;
}

bool __fastcall c_game_manager::font_set__(
	wchar_t c, 
	int font_id, 
	float scale, 
	const char* font_name, 
	int len, 
	int count, 
	int a8, 
	const char* data, 
	int size
) {
#ifdef _DEBUG
	size_t block_size = sizeof(int) * len;
	std::vector<char> buffer(block_size * count);

	for (int i = 0; i < count; ++i) {
		memcpy(
			buffer.data() + block_size * i,
			data + size * i,
			block_size);
	}
#endif

	return false;
}

bool __fastcall c_game_manager::font_get_height(
	int font_id, 
	float scale, 
	const char* font_name, 
	uint16_t* ascender, 
	uint16_t* descender
) {
	return false;
}

bool __fastcall c_game_manager::font____() {
	return false;
}
