#include "font_cache.h"

#include "../main/main.h"
#include <fstream>
#include <freetype/freetype.h>

c_font_cache c_font_cache::g_font_cache;

using namespace libmcc;

static const s_font* get_typeface_font(const s_font* fonts, const s_typeface* typeface, const char* name) {
	for (auto font_index : typeface->fonts) {
		auto font = fonts + font_index;

		if (strcmp(font->name, name)) {
			continue;
		}

		return font;
	}

	return nullptr;
}

c_font_cache::c_font_cache() : 
	m_packer(k_font_texture_width, k_font_texture_height, _rect_pack_method_pixel_scan)
{
}

int c_font_cache::initialize() {
	FT_Init_FreeType(&m_freetype);

	// load font package
	auto font_package_path = std::wstring(main_get_root_folder()) + L"\\font_package.bin";
	std::ifstream font_package_file(font_package_path, std::ios::binary | std::ios::ate);

	font_package_file.seekg(0, std::ios::end);
	auto font_package_size = font_package_file.tellg();
	font_package_file.seekg(0, std::ios::beg);

	if (font_package_size <= 0) {
		return -1;
	}

	auto font_package_buffer = std::make_unique<char[]>(font_package_size);
	font_package_file.read(font_package_buffer.get(), font_package_size);

	m_package = load_font_package(font_package_buffer.get(), font_package_size);
	if (m_package == nullptr) {
		return -1;
	}

	m_faces.resize(m_package->fonts.size());

	return 0;
}

int c_font_cache::shutdown() {
	FT_Done_FreeType(m_freetype);
	m_faces.clear();
	return 0;
}

int c_font_cache::get_kerning_pair_offset(
	wchar_t left, 
	wchar_t right, 
	int size, 
	float scale, 
	const char* name
) {
	for (int i = 0; ; ++i) {
		const s_font* font = nullptr;
		switch (i) {
		case 0:
			font = get_typeface_font(
				m_package->fonts.data(),
				&m_package->default_typeface,
				name);
			break;
		case 1:
			font = get_subtypeface_font(left, name);
			break;
		case 2:
			font = get_typeface_font(
				m_package->fonts.data(),
				&m_package->fallback_typeface,
				name);
			break;
		default:
			return 0;
		}

		if (font == nullptr) {
			continue;
		}

		auto face = get_face(font->asset_index);

		if (face == nullptr) {
			continue;
		}

		auto desired_size = size * scale * 64;

		FT_Set_Char_Size(face, 0, desired_size, 0, 0);

		auto left_index = FT_Get_Char_Index(face, left);
		auto right_index = FT_Get_Char_Index(face, right);

		if (left_index == 0 || right_index == 0) {
			return 0;
		}

		FT_Vector kerning;

		FT_Get_Kerning(face, left_index, right_index, FT_KERNING_DEFAULT, &kerning);
	
		return static_cast<int>(kerning.x / 64.0f * scale);
	}

	return 0;
}

const libmcc::s_font_character* c_font_cache::precache_character(
	wchar_t unicode, 
	uint16_t size, 
	float scale, 
	const char* name
) {
	if (m_package == nullptr) {
		return nullptr;
	}
	
	s_character_info info{
		.unicode = unicode,
		.size = size,
		.scale = scale,
		.font = name
	};

	// check if the character is already cached
	auto it = m_cache.find(info);

	if (it != m_cache.end()) {
		return &it->second;
	}

	auto glyph = render_character(unicode, size, scale, name);

	if (glyph == nullptr) {
		return nullptr;
	}

	auto bitmap = &glyph->bitmap;
	int left, top;
	short width = bitmap->width;
	short height = bitmap->rows;

	char index = m_packer.pack(width, height, &left, &top);

	if (index < 0 || index > m_sprites.size()) {
		return nullptr;
	}

	if (index == m_sprites.size()) {
		m_sprites.push_back(rasterizer()->create_sprite(1024, 1024));
	}

	auto sprite_index = m_sprites[index];

	auto sprite = rasterizer()->get_sprite(sprite_index);

	auto dc = rasterizer()->get_device_context();

	D3D11_BOX dst {
		.left = static_cast<UINT>(left),
		.top = static_cast<UINT>(top),
		.front = 0,
		.right = static_cast<UINT>(left + width),
		.bottom = static_cast<UINT>(top + height),
		.back = 1,
	};

	dc->UpdateSubresource(
		sprite->texture,
		0,
		&dst,
		bitmap->buffer,
		bitmap->pitch,
		bitmap->rows * bitmap->pitch
	);

	auto result = m_cache.emplace(info, s_font_character{});

	if (!result.second) {
		return nullptr;
	}

	short horiBearingX = glyph->metrics.horiBearingX / 64;
	short horiBearingY = glyph->metrics.horiBearingY / 64;
	short horiAdvance = glyph->metrics.horiAdvance / 64;
	short vertBearingX = glyph->metrics.vertBearingX / 64;

	result.first->second = s_font_character{
		.unicode = unicode,
		.scale = scale,
		.left = static_cast<short>(left),
		.top = static_cast<short>(top),
		.width = width,
		.height = height,
		.horiBearingY = horiBearingY,
		.horiBearingX = horiBearingX,
		.horiAdvance = horiAdvance,
		.vertBearingX = vertBearingX,
		.texture = index,
		.valid = true,
	};

	return &result.first->second;
}

FT_FaceRec_* c_font_cache::get_face(int index) {
	auto face = m_faces[index];

	if (face == nullptr) {
		auto& asset = m_package->assets[index];
		auto data = m_package->uncompress_data.data() + asset.offset;

		// load the font face
		auto error = FT_New_Memory_Face(
			m_freetype,
			(const FT_Byte*)data,
			asset.size,
			0,
			&face
		);

		m_faces[index] = face;
	}

	return face;
}

ID3D11ShaderResourceView* c_font_cache::get_texture_resource(int texture_id) {
	auto sprite = rasterizer()->get_sprite(texture_id);

	if (!sprite) {
		return nullptr;
	}

	return sprite->resource;
}

bool c_font_cache::set_selection(
	int size,
	float scale,
	const char* name,
	uint16_t* ascender,
	uint16_t* descender
) {
	auto font = get_typeface_font(
		m_package->fonts.data(), 
		&m_package->default_typeface, 
		name
	);

	if (font == nullptr) {
		return false;
	}

	auto face = get_face(font->asset_index);

	if (face == nullptr) {
		return false;
	}

	auto desired_size = size * scale * 64;

	FT_Set_Char_Size(face, 0, desired_size, 0, 0);

	auto _descender = abs(face->size->metrics.descender / 64);
	auto _ascender = face->size->metrics.ascender / 64 - _descender;

	if (descender) {
		*descender = _descender * scale;
	}

	if (ascender) {
		*ascender = _ascender * scale;
	}

	return true;
}

const struct FT_GlyphSlotRec_* c_font_cache::render_character(
	wchar_t unicode,
	int size,
	float scale,
	const char* name
) {
	for (int i = 0; ; ++i) {
		const s_font* font = nullptr;
		switch (i) {
		case 0:
			font = get_typeface_font(
				m_package->fonts.data(),
				&m_package->default_typeface,
				name);
			break;
		case 1:
			font = get_subtypeface_font(unicode, name);
			break;
		case 2:
			font = get_typeface_font(
				m_package->fonts.data(),
				&m_package->fallback_typeface,
				name);
			break;
		default:
			return nullptr;
		}

		if (font == nullptr) {
			continue;
		}

		auto face = get_face(font->asset_index);

		if (!face) {
			continue;
		}

		if (!face->charmap) {
			continue;
		}

		FT_Error error;
		FT_UInt glyph_index;
		
		glyph_index = FT_Get_Char_Index(face, unicode);

		if (glyph_index == 0) {
			continue;
		}

		auto desired_size = size * scale * 64;

		// set char size
		error = FT_Set_Char_Size(face, 0, desired_size, 0, 0);

		if (error) {
			continue;
		}

		// load glyph
		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);

		if (error) {
			continue;
		}

		// render glyph
		error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);

		if (error) {
			continue;
		}

		return face->glyph;
	}
	return nullptr;
}

const s_font* c_font_cache::get_subtypeface_font(wchar_t unicode, const char* name) {
	for (auto& subtypeface : m_package->subtypefaces) {
		bool hit = false;

		for (auto character_range : subtypeface.character_ranges) {
			auto& range = m_package->character_ranges[character_range];

			if (unicode < *range.begin() || unicode >= *range.end()) {
				continue;
			}

			hit = true;
			break;
		}

		if (!hit) {
			continue;
		}

		auto font = get_typeface_font(
			m_package->fonts.data(),
			&subtypeface.typeface,
			name
		);

		if (font != nullptr) {
			return font;
		}
	}

	return nullptr;
}
