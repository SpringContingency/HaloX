#include "font_cache.h"

#include "../main/main.h"
#include "../logging/logging.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <freetype/freetype.h>
#include <Windows.h>

// Implemented in src/game/game_instance_manager.local.cpp. Returns the cached
// MCC install root (UTF-16) or nullptr if no install was located. Re-declared
// here to avoid a circular #include between text/ and game/.
extern "C" const wchar_t* halox_resolve_mcc_root();

// Resolve halox.exe's directory once and cache it. Used to find the
// per-install `fonts/` override folder. Returns "<dir>" without trailing
// slash, or "" on failure.
static const std::wstring& exe_dir_w() {
	static std::wstring s_dir = []() {
		wchar_t path[MAX_PATH];
		DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return std::wstring();
		std::wstring s(path, n);
		auto slash = s.find_last_of(L"\\/");
		return (slash == std::wstring::npos) ? std::wstring() : s.substr(0, slash);
	}();
	return s_dir;
}

c_font_cache c_font_cache::g_font_cache;

using namespace libmcc;

// System-font substitutions for Halo-specific (licensed) typefaces. Reach,
// halo3, halo4 etc. ask for fonts like "Conduit", "TVNordCond", "HandelGothic"
// that we can't ship in font_package.bin. Map each to the closest geometry-
// matching system TTF on Windows so text renders in something better than
// the package's default Arial.
//
// MCC's own font_package.bin files were investigated (Path A): they exist
// only as `<game>\maps\fonts\font_package_icon[_xN].bin` and are icon-only
// glyph atlases in a different binary format (no zlib, custom directory
// layout) — not loadable by load_font_package(). The actual licensed text
// fonts in MCC are pre-rasterized bitmap glyph files (halo2\h2_fonts\*) or
// embedded inside Scaleform `data\ui\Screens\Fonts\fonts_*.gfx` containers,
// neither of which yields a TTF we can drop in. So we stick with system-font
// substitutes (Path B) until/unless someone provides a halox-format package.
//
// Choices below:
//   - Agency FB Bold (AGENCYB.TTF) is a narrow geometric sans — closest
//     ubiquitous Windows-shipped match to Conduit / TVNord / HandelGothic.
//     Ships with Office and is present on stock Win10/11 in our test env.
//   - Bahnschrift is the next-best fallback (ships on Win10 1709+) — used
//     for Eurostile, which has a slightly different (geometric-square) feel
//     that Agency FB's narrow proportions don't really capture.
//   - Consolas is the modern monospace replacement for Fixedsys.
//
// User overrides — before falling through to the system-font defaults below,
// we look for a same-name TTF in `halox/fonts/<HaloName>.ttf` (relative to
// halox.exe). Drop in any TTF you want to bind a Halo name to (e.g. an
// actual licensed Conduit, or a free lookalike like Saira Condensed /
// Barlow Condensed / Audiowide). The lookup is case-insensitive and
// strips trailing whitespace from the basename.
//
// If a primary path isn't readable at runtime we fall through to the
// secondary; if all candidates fail, find_or_register_substitute returns
// nullptr and the caller falls back to the package's default font (Arial).
struct s_font_substitution {
	const char*    halo_name;
	const wchar_t* windows_ttf_paths[3];  // tried in order, nullptr terminates
};
static const s_font_substitution k_font_substitutions[] = {
	// AGENCYB.TTF was tried as a Path-B substitute but crashes FreeType
	// in tt_face_get_device_metrics on the second FT_Set_Char_Size call —
	// the font ships without one of the optional bitmap/metric tables that
	// FT_Request_Size dereferences (observed: AV at tt_face_get_device_metrics+0x6D
	// with RDX=0). Bahnschrift is a stable substitute that's been tested.
	// All five Halo narrow-geometric typefaces share this substitute.
	{ "TVNordCond",     { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	{ "TVNordCondBold", { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	{ "ConduitMed",     { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	{ "Conduit",        { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	{ "HandelGothic",   { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	{ "Eurostile",      { L"C:\\Windows\\Fonts\\bahnschrift.ttf", nullptr, nullptr } },
	// Consolas is the modern monospace replacement for Fixedsys.
	{ "Fixedsys",       { L"C:\\Windows\\Fonts\\consola.ttf", nullptr, nullptr } },
};

// Halo font name → MCC h2_fonts family stem + native pixel sizes that exist
// on disk. Game requests for a size not in this list snap to the nearest.
struct s_h2_family {
	const char* halo_name;       // matched case-insensitively against requests
	const char* file_stem;       // e.g. "conduit" → conduit-9, conduit-12, conduit-13
	int         sizes[4];        // 0-terminated list of available native sizes
};
static const s_h2_family k_h2_families[] = {
	{ "Conduit",        "conduit",       { 9, 12, 13, 0 } },
	{ "ConduitMed",     "conduit",       { 9, 12, 13, 0 } },
	{ "HandelGothic",   "handel_gothic", { 11, 13, 24, 0 } },
	{ "FixedSys",       "fixedsys",      { 9, 0, 0, 0 } },
	{ "Fixedsys",       "fixedsys",      { 9, 0, 0, 0 } },
	{ "MSLCD",          "MSLCD",         { 14, 0, 0, 0 } },
	// TVNordCond / Eurostile have no h2_fonts equivalent — they're newer
	// MCC additions; those fall through to the TTF substitute path.
};

c_h2_bitmap_font* c_font_cache::find_or_load_h2_bitmap(const char* name, int size) {
	if (!name || size <= 0) return nullptr;

	const s_h2_family* fam = nullptr;
	for (const auto& f : k_h2_families) {
		if (_stricmp(f.halo_name, name) == 0) { fam = &f; break; }
	}
	if (!fam) return nullptr;

	// Snap requested size to closest native size for this family. Sub-9pt
	// requests (rare) round up to 9 — h2 bitmap atlases have no smaller tier.
	int best = fam->sizes[0];
	int best_d = std::abs(size - best);
	for (int i = 1; i < 4 && fam->sizes[i] > 0; ++i) {
		int d = std::abs(size - fam->sizes[i]);
		if (d < best_d) { best = fam->sizes[i]; best_d = d; }
	}

	char key[64];
	std::snprintf(key, sizeof(key), "%s-%d", fam->file_stem, best);
	auto it = m_h2_fonts.find(key);
	if (it != m_h2_fonts.end()) return it->second.get();  // may be nullptr (negative cache)

	// Build absolute path: <MCC_ROOT>\halo2\h2_fonts\<stem>-<size>
	const wchar_t* mcc = halox_resolve_mcc_root();
	if (!mcc) {
		m_h2_fonts.emplace(key, nullptr);  // negative-cache so we don't re-resolve every call
		return nullptr;
	}
	std::wstring path = mcc;
	path += L"\\halo2\\h2_fonts\\";
	for (const char* c = fam->file_stem; *c; ++c) path.push_back((wchar_t)(unsigned char)*c);
	path += L'-';
	wchar_t numbuf[8];
	swprintf(numbuf, 8, L"%d", best);
	path += numbuf;

	auto loaded = c_h2_bitmap_font::load(path);
	if (!loaded) {
		CONSOLE_LOG_WARN("h2_bitmap_font: failed to load %ls — falling back to TTF substitute for '%s'",
			path.c_str(), name);
		m_h2_fonts.emplace(key, nullptr);
		return nullptr;
	}
	loaded->set_design_size(best);  // filename size — used as render-scale denominator
	auto* raw = loaded.get();
	m_h2_fonts.emplace(key, std::move(loaded));
	return raw;
}

const libmcc::s_font_character* c_font_cache::upload_h2_glyph(
	const s_h2_glyph& glyph, const s_character_info& info, float render_scale)
{
	if (render_scale <= 0.0f) render_scale = 1.0f;
	auto scale_round = [render_scale](int v) {
		return (int)(v * render_scale + 0.5f);
	};

	const int src_w = glyph.width;
	const int src_h = glyph.height;
	int dst_w = scale_round(src_w);
	int dst_h = scale_round(src_h);

	// Empty glyph (e.g. SPACE) — register a zero-width entry so the engine
	// still gets advance/metrics without trying to texture-pack a 0×0 rect.
	if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
		auto result = m_cache.emplace(info, libmcc::s_font_character{});
		if (!result.second) return nullptr;
		result.first->second = libmcc::s_font_character{
			.unicode = info.unicode,
			.scale = info.scale,
			.left = 0,
			.top = 0,
			.width = 0,
			.height = 0,
			.horiBearingY = (short)scale_round(glyph.origin_y),
			.horiBearingX = (short)scale_round(glyph.origin_x),
			.horiAdvance  = (short)scale_round(glyph.display_width),
			.vertBearingX = 0,
			.texture = 0,
			.valid = true,
		};
		return &result.first->second;
	}

	// Pack with a 1px transparent gutter so bilinear sampling at glyph edges
	// blends to the (zero-init / never-written) border instead of bleeding
	// into adjacent packed glyphs. The reported atlas (left, top) points at
	// the glyph content; the gutter sits outside the engine's source rect.
	constexpr int k_glyph_padding = 1;
	int slot_left = 0, slot_top = 0;
	char index = m_packer.pack((short)(dst_w + 2 * k_glyph_padding),
		(short)(dst_h + 2 * k_glyph_padding), &slot_left, &slot_top);
	if (index < 0 || index > (char)m_sprites.size()) return nullptr;
	if ((size_t)index == m_sprites.size())
		m_sprites.push_back(rasterizer()->create_sprite(1024, 1024));
	int left = slot_left + k_glyph_padding;
	int top  = slot_top  + k_glyph_padding;

	auto sprite_index = m_sprites[index];
	auto sprite = rasterizer()->get_sprite(sprite_index);
	auto dc = rasterizer()->get_device_context();

	D3D11_BOX dst{
		.left = (UINT)left, .top = (UINT)top, .front = 0,
		.right = (UINT)(left + dst_w), .bottom = (UINT)(top + dst_h), .back = 1,
	};
	// Atlas is RGBA8 — engine text shaders read .rgb as glyph color. Resample
	// the source alpha (nearest-neighbor) and inflate to white-with-alpha.
	// The h2 atlases are per-fixed-size; the engine asks for arbitrary sizes,
	// so without this rescale all halo2 text overflows its layout box.
	std::vector<uint32_t> rgba((size_t)dst_w * dst_h);
	for (int y = 0; y < dst_h; ++y) {
		int sy = (int)((y + 0.5f) / render_scale);
		if (sy >= src_h) sy = src_h - 1;
		const uint8_t* src_row = glyph.alpha.data() + (size_t)sy * src_w;
		uint32_t* dst_row = rgba.data() + (size_t)y * dst_w;
		for (int x = 0; x < dst_w; ++x) {
			int sx = (int)((x + 0.5f) / render_scale);
			if (sx >= src_w) sx = src_w - 1;
			dst_row[x] = 0x00FFFFFFu | ((uint32_t)src_row[sx] << 24);
		}
	}
	dc->UpdateSubresource(sprite->texture, 0, &dst,
		rgba.data(), (UINT)(dst_w * 4), (UINT)(dst_w * dst_h * 4));

	auto result = m_cache.emplace(info, libmcc::s_font_character{});
	if (!result.second) return nullptr;
	result.first->second = libmcc::s_font_character{
		.unicode = info.unicode,
		.scale = info.scale,
		.left = (short)left,
		.top = (short)top,
		.width = (short)dst_w,
		.height = (short)dst_h,
		.horiBearingY = (short)scale_round(glyph.origin_y),
		.horiBearingX = (short)scale_round(glyph.origin_x),
		.horiAdvance  = (short)scale_round(glyph.display_width),
		.vertBearingX = 0,
		.texture = index,
		.valid = true,
	};
	return &result.first->second;
}

const s_font* c_font_cache::find_or_register_substitute(const char* name) {
	if (!m_package || !name) return nullptr;
	auto it = m_substitute_index.find(name);
	if (it != m_substitute_index.end()) {
		int idx = it->second;
		return (idx >= 0) ? &m_package->fonts[idx] : nullptr;
	}

	const s_font_substitution* sub_entry = nullptr;
	for (const auto& sub : k_font_substitutions) {
		if (_stricmp(sub.halo_name, name) == 0) {
			sub_entry = &sub;
			break;
		}
	}
	// User-override path runs even for names NOT in the table: drop any TTF
	// at `<halox.exe dir>/fonts/<name>.ttf` and we'll bind it to that name.
	// Lets a user add a font without code changes (e.g. licensed Conduit, or
	// a free Conduit lookalike). Win32 paths are case-insensitive so we
	// don't need case variants.
	std::ifstream in;
	std::wstring  override_path;
	const std::wstring& dir = exe_dir_w();
	if (!dir.empty()) {
		// Compose `<dir>\fonts\<name>.ttf`. Convert ASCII name to wide.
		std::wstring p = dir + L"\\fonts\\";
		for (const char* c = name; *c; ++c) p.push_back((wchar_t)(unsigned char)*c);
		p += L".ttf";
		in.open(p, std::ios::binary | std::ios::ate);
		if (in) override_path = std::move(p);
		else    in.clear();
	}

	if (override_path.empty() && !sub_entry) {
		// No override file AND no system-font default for this name.
		m_substitute_index[name] = -1;  // negative cache
		return nullptr;
	}

	// If no override matched, walk the system-font candidate list.
	const wchar_t* ttf_path = override_path.empty() ? nullptr : override_path.c_str();
	if (!ttf_path) {
		for (auto* candidate : sub_entry->windows_ttf_paths) {
			if (!candidate) break;
			in.open(candidate, std::ios::binary | std::ios::ate);
			if (in) { ttf_path = candidate; break; }
			in.clear();
		}
	}
	if (!ttf_path) {
		CONSOLE_LOG_WARN("font substitute: no readable TTF found for '%s' — falling back to package default",
			name);
		m_substitute_index[name] = -1;
		return nullptr;
	}
	auto size = (size_t)in.tellg();
	in.seekg(0, std::ios::beg);

	// CRITICAL: FT_New_Memory_Face holds a raw pointer INTO this vector's
	// buffer. Any later resize() that reallocates makes every prior face's
	// pointer dangle (observed: tt_face_get_device_metrics+0x6D crashes on
	// the second substitute). Reserve one absolute ceiling on first call so
	// every subsequent append fits inside the existing allocation —
	// repeated incremental reserves all reallocate, defeating the purpose.
	constexpr size_t k_substitute_reserve = 256 * 1024 * 1024;
	if (m_package->uncompress_data.capacity() < k_substitute_reserve) {
		m_package->uncompress_data.reserve(k_substitute_reserve);
	}
	// Hard refuse if a substitute would push us past the ceiling — don't
	// silently reallocate.
	if (m_package->uncompress_data.size() + size > k_substitute_reserve) {
		CONSOLE_LOG_WARN("font substitute: reserve ceiling (%zu MB) reached, refusing '%s'",
			k_substitute_reserve / (1024 * 1024), name);
		m_substitute_index[name] = -1;
		return nullptr;
	}

	// Append the raw TTF bytes to the package's uncompressed-data blob.
	uint32_t offset = (uint32_t)m_package->uncompress_data.size();
	m_package->uncompress_data.resize(offset + size);
	in.read(m_package->uncompress_data.data() + offset, size);

	s_font_asset asset{};
	asset.size   = (uint32_t)size;
	asset.offset = offset;
	m_package->assets.push_back(asset);

	s_font font{};
	std::strncpy(font.name, name, sizeof(font.name) - 1);
	int asset_idx = (int)m_package->assets.size() - 1;
	if (asset_idx > 0x7F) {
		// s_font::asset_index is int8_t. We've blown the cap — bail rather
		// than wrap to a negative value and return a different font.
		m_package->assets.pop_back();
		m_package->uncompress_data.resize(offset);
		CONSOLE_LOG_WARN("font substitute: asset_index overflow (>127), can't register '%s'", name);
		m_substitute_index[name] = -1;
		return nullptr;
	}
	font.asset_index = (int8_t)asset_idx;
	int font_idx = (int)m_package->fonts.size();
	m_package->fonts.push_back(font);
	m_faces.push_back(nullptr);  // grow lazily-cached face slots in lockstep

	m_substitute_index[name] = font_idx;
	CONSOLE_LOG_INFO("font substitute: '%s' -> %ls (font_index=%d, asset_index=%d, %zu bytes)",
		name, ttf_path, font_idx, asset_idx, size);
	return &m_package->fonts[font_idx];
}

static const s_font* get_typeface_font(const s_font* fonts, const s_typeface* typeface, const char* name) {
	const s_font* first = nullptr;
	for (auto font_index : typeface->fonts) {
		auto font = fonts + font_index;
		if (!first) first = font;
		if (name && strcmp(font->name, name) == 0) {
			return font;
		}
	}
	// Fallback: when the requested font name isn't in the package, return the
	// first available font in the typeface so something still renders. Halo's
	// runtime asks for many specific font names that our minimal arial-derived
	// package doesn't actually contain. (Callers consult find_or_register_substitute
	// first to try a system-font substitution before reaching this fallback.)
	return first;
}

c_font_cache::c_font_cache() : 
	m_packer(k_font_texture_width, k_font_texture_height, _rect_pack_method_pixel_scan)
{
}

int c_font_cache::initialize() {
	FT_Init_FreeType(&m_freetype);

	// Look for font_package.bin next to the executable first (where the
	// `tool import_ttf` output lands), then fall back to Documents\halox\.
	wchar_t exe_dir[MAX_PATH];
	GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
	if (auto last_slash = wcsrchr(exe_dir, L'\\')) *last_slash = L'\0';

	std::wstring candidates[] = {
		std::wstring(exe_dir) + L"\\font_package.bin",
		std::wstring(main_get_root_folder()) + L"\\font_package.bin",
	};

	std::ifstream font_package_file;
	std::wstring chosen;
	for (const auto& path : candidates) {
		font_package_file.open(path, std::ios::binary | std::ios::ate);
		if (font_package_file) { chosen = path; break; }
		font_package_file.clear();
	}

	if (!font_package_file) {
		CONSOLE_LOG_WARN("font_package.bin: not found (looked next to halox.exe and in %%USERPROFILE%%\\Documents\\halox)");
		return -1;
	}

	auto font_package_size = font_package_file.tellg();
	font_package_file.seekg(0, std::ios::beg);
	if (font_package_size <= 0) {
		CONSOLE_LOG_WARN("font_package.bin: empty file at %ls", chosen.c_str());
		return -1;
	}

	auto font_package_buffer = std::make_unique<char[]>(font_package_size);
	font_package_file.read(font_package_buffer.get(), font_package_size);

	m_package = load_font_package(font_package_buffer.get(), font_package_size);
	if (!m_package) {
		CONSOLE_LOG_WARN("font_package.bin: parse failed (%ls, %lld bytes)",
			chosen.c_str(), (long long)font_package_size);
		return -1;
	}

	m_faces.resize(m_package->fonts.size());
	CONSOLE_LOG_INFO("font_package.bin: loaded %ls (%zu fonts, %zu assets)",
		chosen.c_str(), m_package->fonts.size(), m_package->assets.size());
	// Dump the names of the fonts in our package — useful when comparing
	// against the names games pass to font_set_selection / font_test_char.
	// If a game asks for a name we don't have, get_typeface_font falls back
	// to the first font, but seeing the mismatch in the log makes the cause
	// obvious.
	for (size_t i = 0; i < m_package->fonts.size(); ++i) {
		CONSOLE_LOG_INFO("  font[%zu]: name='%.31s' asset_index=%d",
			i, m_package->fonts[i].name, (int)m_package->fonts[i].asset_index);
	}
	return 0;
}

int c_font_cache::shutdown() {
	if (m_package) {
		FT_Done_FreeType(m_freetype);
		m_faces.clear();
		m_package.reset();
	}
	return 0;
}

int c_font_cache::get_kerning_pair_offset(
	wchar_t left,
	wchar_t right,
	int size,
	float scale,
	const char* name
) {
	if (m_package == nullptr) return 0;
	// h2 bitmap font path: kerning is per-pair lookup; scale the result by
	// the same render_scale used for glyph metrics so it stays consistent.
	int requested_px = (int)(size * scale + 0.5f);
	if (auto h2 = find_or_load_h2_bitmap(name, requested_px)) {
		int unit = h2->ascend();
		float render_scale = (unit > 0)
			? (float)requested_px / (float)unit
			: 1.0f;
		return (int)(h2->kerning(left, right) * render_scale + 0.5f);
	}
	if (auto sub = find_or_register_substitute(name)) {
		auto face = get_face(sub->asset_index);
		if (face) {
			FT_Set_Char_Size(face, 0, (FT_F26Dot6)(size * scale * 64), 0, 0);
			auto li = FT_Get_Char_Index(face, left);
			auto ri = FT_Get_Char_Index(face, right);
			if (li && ri) {
				FT_Vector k;
				FT_Get_Kerning(face, li, ri, FT_KERNING_DEFAULT, &k);
				return static_cast<int>(k.x / 64.0f * scale);
			}
			return 0;
		}
	}
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

	// h2 bitmap font path: render glyph from MCC's pre-rasterized per-size
	// atlas, resampled to match the engine's requested size. The atlases are
	// per-fixed-size (conduit-9/12/13, handel_gothic-11/13/24, ...); the
	// engine asks for arbitrary sizes (e.g. 18, 22, 26, 48), so we snap to
	// the closest available file and rescale every glyph to fit.
	int requested_px = (int)(size * scale + 0.5f);
	if (auto h2 = find_or_load_h2_bitmap(name, requested_px)) {
		if (auto* g = h2->find_glyph(unicode)) {
			// AscendHeight is the typeface convention for "size": engines
			// treat `size` as the cap-/ascender-height the renderer should
			// produce. Filename design_size is too small (yields 2× native);
			// total bitmap height (ascend+descend) is too large (yields
			// 0.6× native). Ascend lands the visual size at the engine's
			// expectation and makes returned ascender == requested size.
			int unit = h2->ascend();
			float render_scale = (unit > 0)
				? (float)requested_px / (float)unit
				: 1.0f;
			return upload_h2_glyph(*g, info, render_scale);
		}
		// Glyph missing in this h2 atlas (e.g. exotic codepoint). Fall through
		// to the FreeType path so the substitute TTF can fill the gap.
	}

	auto glyph = render_character(unicode, size, scale, name);

	if (glyph == nullptr) {
		return nullptr;
	}

	auto bitmap = &glyph->bitmap;
	short width = bitmap->width;
	short height = bitmap->rows;

	// Pack with a 1px transparent gutter so bilinear sampling at glyph edges
	// blends to the (zero-init / never-written) border instead of bleeding
	// into adjacent packed glyphs. The reported atlas (left, top) points at
	// the glyph content; the gutter sits outside the engine's source rect.
	constexpr int k_glyph_padding = 1;
	int slot_left = 0, slot_top = 0;
	char index = m_packer.pack(width + 2 * k_glyph_padding,
		height + 2 * k_glyph_padding, &slot_left, &slot_top);

	if (index < 0 || index > m_sprites.size()) {
		return nullptr;
	}

	if (index == m_sprites.size()) {
		m_sprites.push_back(rasterizer()->create_sprite(1024, 1024));
	}

	int left = slot_left + k_glyph_padding;
	int top  = slot_top  + k_glyph_padding;

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

	// Atlas is RGBA8 — see comment in upload_h2_glyph(). FreeType FT_RENDER_MODE_NORMAL
	// produces a single-byte grayscale buffer with `bitmap->pitch` row stride
	// (may exceed `bitmap->width` due to FT alignment). Inflate to 4-byte
	// white-with-alpha, packed tightly at width*4.
	std::vector<uint32_t> rgba((size_t)width * height);
	for (int y = 0; y < height; ++y) {
		const uint8_t* src = bitmap->buffer + (size_t)y * bitmap->pitch;
		uint32_t* dst_row = rgba.data() + (size_t)y * width;
		for (int x = 0; x < width; ++x)
			dst_row[x] = 0x00FFFFFFu | ((uint32_t)src[x] << 24);
	}
	dc->UpdateSubresource(
		sprite->texture,
		0,
		&dst,
		rgba.data(),
		(UINT)(width * 4),
		(UINT)(width * height * 4)
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
	if (m_package == nullptr || index < 0 || index >= (int)m_faces.size()) return nullptr;
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
	if (m_package == nullptr) {
		if (ascender)  *ascender = 0;
		if (descender) *descender = 0;
		return false;
	}
	// h2 bitmap font path: scale by AscendHeight (the typeface's
	// above-baseline extent in pixels). Engine `size` semantically equals
	// "make the ascender this tall". Returns scaled ascender = requested
	// size exactly, scaled descender proportionally. Tested alternatives:
	// design_size (filename) → 2× too big; ascend+descend → ~40% too small.
	int requested_px = (int)(size * scale + 0.5f);
	if (auto h2 = find_or_load_h2_bitmap(name, requested_px)) {
		int unit = h2->ascend();
		float render_scale = (unit > 0)
			? (float)requested_px / (float)unit
			: 1.0f;
		if (ascender)  *ascender  = (uint16_t)(h2->ascend()  * render_scale + 0.5f);
		if (descender) *descender = (uint16_t)(h2->descend() * render_scale + 0.5f);
		return true;
	}
	auto font = find_or_register_substitute(name);
	if (font == nullptr) {
		font = get_typeface_font(
			m_package->fonts.data(),
			&m_package->default_typeface,
			name
		);
	}

	if (font == nullptr) {
		return false;
	}

	auto face = get_face(font->asset_index);

	if (face == nullptr) {
		return false;
	}

	auto desired_size = size * scale * 64;

	FT_Set_Char_Size(face, 0, desired_size, 0, 0);

	// FreeType reports both metrics in 26.6 fixed-point; descender is negative.
	// Ascender is the full above-baseline distance — do NOT subtract descender
	// from it (the old code did, which under-reported font height and made
	// multi-line UI like the scoreboard render with wrong line spacing).
	auto _ascender  = face->size->metrics.ascender  / 64;
	auto _descender = abs(face->size->metrics.descender / 64);

	if (descender) {
		*descender = (uint16_t)(_descender * scale);
	}
	if (ascender) {
		*ascender = (uint16_t)(_ascender * scale);
	}

	return true;
}

const struct FT_GlyphSlotRec_* c_font_cache::render_character(
	wchar_t unicode,
	int size,
	float scale,
	const char* name
) {
	if (m_package == nullptr) return nullptr;
	// Stage -1 (substitute) runs first so games asking for Conduit/TVNord/etc
	// hit the system-font replacement before the package-default fallback.
	for (int i = -1; ; ++i) {
		const s_font* font = nullptr;
		switch (i) {
		case -1:
			font = find_or_register_substitute(name);
			break;
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
	if (m_package == nullptr) return nullptr;
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
