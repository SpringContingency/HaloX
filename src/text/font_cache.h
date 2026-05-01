#pragma once

#include "../rasterizer/rasterizer.h"
#include "../geometry/rect_pack.h"
#include "font_package.h"
#include "halo2_bitmap_font.h"

#include <memory>
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

	// Lazy-load a Windows system TTF for a Halo-specific font name (Conduit,
	// HandelGothic, TVNordCond, Fixedsys, Eurostile…). Appends the TTF as a
	// synthetic font into m_package and returns the new s_font*, or nullptr
	// if no substitution is configured / the system file isn't readable.
	const s_font* find_or_register_substitute(const char* name);

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

	// Look up (or lazy-load) MCC's halo2 bitmap font for `name`+`size`. Maps
	// halox font names (Conduit, ConduitMed, HandelGothic, FixedSys, MSLCD)
	// to the corresponding `<MCC>/halo2/h2_fonts/<family>-<size>` file,
	// snapping `size` to the closest available native size for that family.
	// Returns nullptr if MCC root unresolvable, the family has no h2_fonts
	// equivalent, or the file is missing/malformed (negative cache prevents
	// retrying the same key).
	c_h2_bitmap_font* find_or_load_h2_bitmap(const char* name, int size);

	static c_font_cache g_font_cache;

private:
	c_rect_packer m_packer;
	std::vector<int> m_sprites;
	struct FT_LibraryRec_* m_freetype;
	std::vector<struct FT_FaceRec_*> m_faces;
	std::unique_ptr<s_runtime_font_package> m_package;
	std::unordered_map<s_character_info, libmcc::s_font_character> m_cache;

	// Negative cache sentinel = -1; otherwise the index into m_package->fonts
	// of the lazily-registered system-TTF substitute for that name.
	std::unordered_map<std::string, int> m_substitute_index;

	// Lazy-loaded halo2 bitmap fonts, keyed by "<family>-<size>" (e.g.
	// "conduit-13"). nullptr value = negative cache (load attempted, failed).
	std::unordered_map<std::string, std::unique_ptr<c_h2_bitmap_font>> m_h2_fonts;

	// Pack a single h2 glyph into the atlas and build its s_font_character
	// metrics. Used by precache_character when find_or_load_h2_bitmap hits.
	// `render_scale` resamples the glyph bitmap (nearest-neighbor) and
	// proportionally scales every metric — needed because h2 atlases are
	// per-fixed-pixel-size and the engine asks for arbitrary sizes.
	const libmcc::s_font_character* upload_h2_glyph(
		const s_h2_glyph& glyph,
		const s_character_info& info,
		float render_scale);
};

inline c_font_cache* font_cache() {
	return &c_font_cache::g_font_cache;
}