#include "game_manager.h"

#include "../text/font_cache.h"

using namespace libmcc;

bool __fastcall c_game_manager::font__() {
	return false;
}

bool __fastcall c_game_manager::font___() {
	return false;
}

bool __fastcall c_game_manager::font_test_string(
	const wchar_t* str, 
	int font_size, 
	float scale, 
	const char* font_name
) {
	return true;
}

bool __fastcall c_game_manager::font_precache_character(
	wchar_t unicode, 
	s_font_character* character, 
	int size, 
	float scale, 
	const char* name
) {
	auto result = font_cache()->precache_character(unicode, size, scale, name);

	if (!result) {
		return false;
	}

	memcpy(character, result, sizeof(s_font_character));

	return true;
}

ID3D11ShaderResourceView* __fastcall c_game_manager::font_get_texture(
	int texture_id
) {
	return font_cache()->get_texture_resource(texture_id);
}

bool __fastcall c_game_manager::font_test_char(
	wchar_t c, 
	int size,
	float scale, 
	const char* font_name
) {
	return false;
}

int __fastcall c_game_manager::font_get_kerning_pair_offset(
	wchar_t left, 
	wchar_t right,
	int size, 
	float scale, 
	const char* name
) {
	return font_cache()->get_kerning_pair_offset(left, right, size, scale, name);
}

bool __fastcall c_game_manager::font_set__(
	wchar_t c, 
	int font_size, 
	float scale, 
	const char* font_name, 
	int len, 
	int count, 
	int a8, 
	const char* data, 
	int size
) {
#if 0
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

bool __fastcall libmcc::c_game_manager::font_set_selection(
	int size, 
	float scale, 
	const char* font_name, 
	uint16_t* ascender, 
	uint16_t* descender
) {
	return font_cache()->set_selection(
		size,
		scale,
		font_name,
		ascender,
		descender
	);
}

bool __fastcall c_game_manager::font____() {
	return false;
}
