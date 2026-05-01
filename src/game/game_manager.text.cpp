#include "game_manager.h"

#include "../text/font_cache.h"
#include "../logging/logging.h"

#include <atomic>

using namespace libmcc;

// One-shot diagnostic logging for the font path. Reach text "doesn't render
// properly" — we don't yet know whether the font_name strings differ from
// halo3, the size/scale differs, a vtable method is missing, or something
// else. These counters log a few representative calls so the user's halox.log
// shows the actual arguments Reach is passing into our text vtable.
//
// Cap each log site so a per-frame call doesn't flood the file.
static constexpr int k_font_log_max_per_site = 12;

static std::atomic<int> g_log_count_set_selection{0};
static std::atomic<int> g_log_count_precache{0};
static std::atomic<int> g_log_count_test_char{0};
static std::atomic<int> g_log_count_test_string{0};
static std::atomic<int> g_log_count_kerning{0};
static std::atomic<int> g_log_count_set_{0};

static const char* safe_font_name(const char* name) {
	// Reach (and halo3) sometimes pass nullptr or even an integer cast to
	// const char*. Rather than crash strncpy(), surface what we got.
	if (!name) return "(null)";
	// Heuristic: pointer below a reasonable user-mode threshold means it's
	// almost certainly an integer-encoded font id, not a real char*.
	if ((uintptr_t)name < 0x10000) return "(int-id)";
	return name;
}

static int safe_font_name_int(const char* name) {
	// If the "name" is small enough to be an integer, return it as such.
	if ((uintptr_t)name < 0x10000) return (int)(uintptr_t)name;
	return -1;
}

bool __fastcall c_game_manager::font__() {
	// "font system ready" probes — claim yes so halo3 doesn't bail on text.
	return true;
}

bool __fastcall c_game_manager::font___() {
	return true;
}

bool __fastcall c_game_manager::font_test_string(
	const wchar_t* str,
	int font_size,
	float scale,
	const char* font_name
) {
	if (g_log_count_test_string.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(font_name);
		CONSOLE_LOG_INFO(
			"font_test_string: size=%d scale=%.3f name='%s' (intId=%d)",
			font_size, scale, safe_font_name(font_name), idx);
	}
	// Always reachable as long as the font cache loaded.
	return true;
}

bool __fastcall c_game_manager::font_precache_character(
	wchar_t unicode,
	s_font_character* character,
	int size,
	float scale,
	const char* name
) {
	if (g_log_count_precache.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(name);
		CONSOLE_LOG_INFO(
			"font_precache_character: U+%04X size=%d scale=%.3f name='%s' (intId=%d) charPtr=%p",
			(unsigned)unicode, size, scale, safe_font_name(name), idx, (void*)character);
	}

	auto result = font_cache()->precache_character(unicode, size, scale, name);

	if (!result) {
		if (g_log_count_precache.load() <= k_font_log_max_per_site) {
			CONSOLE_LOG_WARN(
				"font_precache_character: FAILED for U+%04X size=%d scale=%.3f name='%s'",
				(unsigned)unicode, size, scale, safe_font_name(name));
		}
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
	if (g_log_count_test_char.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(font_name);
		CONSOLE_LOG_INFO(
			"font_test_char: U+%04X size=%d scale=%.3f name='%s' (intId=%d)",
			(unsigned)c, size, scale, safe_font_name(font_name), idx);
	}
	// Halo asks "can the font render this glyph?" before rendering each char.
	// Returning false makes it substitute placeholders. Defer to the cache —
	// if precaching the char succeeds we're good.
	return font_cache()->precache_character(c, (uint16_t)size, scale, font_name) != nullptr;
}

int __fastcall c_game_manager::font_get_kerning_pair_offset(
	wchar_t left,
	wchar_t right,
	int size,
	float scale,
	const char* name
) {
	if (g_log_count_kerning.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(name);
		CONSOLE_LOG_INFO(
			"font_get_kerning: U+%04X,U+%04X size=%d scale=%.3f name='%s' (intId=%d)",
			(unsigned)left, (unsigned)right, size, scale, safe_font_name(name), idx);
	}
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
	if (g_log_count_set_.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(font_name);
		CONSOLE_LOG_INFO(
			"font_set__: U+%04X size=%d scale=%.3f name='%s' (intId=%d) len=%d count=%d a8=%d size=%d",
			(unsigned)c, font_size, scale, safe_font_name(font_name), idx,
			len, count, a8, size);
	}
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
	bool ok = font_cache()->set_selection(
		size,
		scale,
		font_name,
		ascender,
		descender
	);

	if (g_log_count_set_selection.fetch_add(1) < k_font_log_max_per_site) {
		int idx = safe_font_name_int(font_name);
		uint16_t asc = ascender ? *ascender : 0;
		uint16_t desc = descender ? *descender : 0;
		CONSOLE_LOG_INFO(
			"font_set_selection: size=%d scale=%.3f name='%s' (intId=%d) -> ok=%d ascender=%u descender=%u",
			size, scale, safe_font_name(font_name), idx, (int)ok, asc, desc);
	}

	return ok;
}

bool __fastcall c_game_manager::font____() {
	return true;
}
