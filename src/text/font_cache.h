#pragma once

#include "../rasterizer/rasterizer.h"
#include "../geometry/rect_pack.h"
#include "font_package.h"

#include <unordered_set>
#include <libmcc/libmcc.h>

struct s_character_info {
	wchar_t unicode;
	uint16_t size;
	float scale;
	std::string font;

	bool operator ==(const s_character_info& other) const {
		return 
			unicode == other.unicode &&
			size == other.size &&
			scale == other.scale &&
			font == other.font;
	}
};

namespace std {
	template<>
	struct hash<s_character_info> {
		size_t operator()(const s_character_info& key) const {
			union {
				struct {
					wchar_t unicode;
					uint16_t size;
					float scale;
				};
				size_t value;
			} result;

			auto hash = std::hash<std::string>()(key.font);

			result.unicode = key.unicode;
			result.size = key.size;
			result.scale = key.scale;

			return result.value ^ hash;
		}
	};
}

class c_font_cache {
public:
	c_font_cache();

	int initialize();
	int shutdown();

	int get_kerning_pair_offset(
		wchar_t left,
		wchar_t right,
		int size,
		float scale,
		const char* name
	);

	const libmcc::s_font_character* precache_character(
		wchar_t unicode,
		uint16_t size,
		float scale,
		const char* name
	);

	struct FT_FaceRec_* get_face(int index);

	const struct FT_GlyphSlotRec_* render_character(
		wchar_t unicode,
		int size, 
		float scale,
		const char* name
	);

	ID3D11ShaderResourceView* get_texture_resource(int texture_id);

	bool set_selection(
		int size, 
		float scale, 
		const char* font_name, 
		uint16_t* ascender, 
		uint16_t* descender
	);

	const s_font* get_subtypeface_font(wchar_t unicode, const char* name);

	static c_font_cache g_font_cache;

private:
	c_rect_packer m_packer;
	std::vector<int> m_sprites;
	struct FT_LibraryRec_* m_freetype;
	std::vector<struct FT_FaceRec_*> m_faces;
	std::unique_ptr<s_runtime_font_package> m_package;
	std::unordered_map<s_character_info, libmcc::s_font_character> m_cache;
};

inline c_font_cache* font_cache() {
	return &c_font_cache::g_font_cache;
}