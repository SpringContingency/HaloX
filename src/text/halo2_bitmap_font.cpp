#include "halo2_bitmap_font.h"

#include "../logging/logging.h"

#include <fstream>
#include <cstring>
#include <algorithm>

namespace {

// Decompress one character's RLE byte stream into a BGRA32 pixel buffer.
// Algorithm ported from Lord-Zedd/FontPackager Classes/CharacterTools.cs.
// Returns true if the produced byte count matches expected_pixels*4 exactly;
// `out` is filled regardless so the caller can salvage partial data if needed.
bool decompress_h2_glyph(const uint8_t* data, size_t len,
                         size_t expected_pixels,
                         std::vector<uint8_t>& out) {
	out.clear();
	out.reserve(expected_pixels * 4);

	// Helper: append `count` BGRA pixels with the given 16-bit packed color.
	// Packed color layout: AAAA RRRR GGGG BBBB (4 bits per channel).
	// Decompressed atlas is BGRA32, each channel scaled (n * 255 / 15).
	auto write_run = [&out](int count, uint16_t color) {
		uint8_t b = (uint8_t)(((color >> 0)  & 0xF) * 255 / 15);
		uint8_t g = (uint8_t)(((color >> 4)  & 0xF) * 255 / 15);
		uint8_t r = (uint8_t)(((color >> 8)  & 0xF) * 255 / 15);
		uint8_t a = (uint8_t)(((color >> 12) & 0xF) * 255 / 15);
		for (int i = 0; i < count; ++i) {
			out.push_back(b);
			out.push_back(g);
			out.push_back(r);
			out.push_back(a);
		}
	};
	// Skip pixCount destination pixels (advance write head over them, leaving
	// zeroed bytes). The C# version just bumps stream Position; in our backing
	// vector we pad with zero-alpha transparent pixels.
	auto skip_pixels = [&out](int count) {
		for (int i = 0; i < count; ++i) {
			out.push_back(0);
			out.push_back(0);
			out.push_back(0);
			out.push_back(0);
		}
	};

	uint16_t base_color = 0xFFF;  // baseline RGB only (alpha tracked separately)

	for (size_t i = 0; i < len; ++i) {
		uint8_t code = data[i];
		uint8_t code_flags = (uint8_t)(code >> 6);
		uint8_t code_value = (uint8_t)(code & 0x3F);

		if (code_flags < 2) {
			if (code == 0) {
				// Two-byte literal color follows: AR GB packed BE.
				if (i + 2 >= len) return false;
				uint16_t new_color = (uint16_t)((data[i + 1] << 8) | data[i + 2]);
				i += 2;
				write_run(1, new_color);
				base_color = (uint16_t)(new_color & 0xFFF);
			} else {
				int pix_count = code_value;
				uint16_t or_val = (code_flags == 0) ? 0x0000 : 0xF000;
				write_run(pix_count, (uint16_t)(base_color | or_val));
			}
			continue;
		}

		// codeFlags >= 2 — short two-pixel form, or codeFlags==2 special case.
		uint16_t three_bit_a = (uint16_t)((code >> 3) & 7);
		uint16_t a_alpha = (uint16_t)((three_bit_a << 1) | (three_bit_a & 1));
		uint16_t color_a = (uint16_t)((a_alpha << 12) | base_color);
		write_run(1, color_a);

		uint16_t three_bit_b = (uint16_t)(code & 7);

		if (code_flags != 2) {
			uint16_t b_alpha = (uint16_t)((three_bit_b << 1) | (three_bit_b & 1));
			uint16_t color_b = (uint16_t)((b_alpha << 12) | base_color);
			write_run(1, color_b);
		} else if (three_bit_b == 0) {
			// In the C# original this advances the *output stream Position* by
			// `pixCount` — but pixCount is 0 here (it was assigned only in the
			// codeFlags<2 branch above). So this is a no-op in practice. Mirror
			// that behavior.
		} else {
			uint16_t new_color = base_color;
			if ((three_bit_b & 4) == 0)
				new_color = (uint16_t)(base_color | 0xF000);
			int pix_count = std::abs(5 - (int)(three_bit_b & 3));
			write_run(pix_count, new_color);
		}
	}

	return out.size() == expected_pixels * 4;
}

inline int16_t  rd_i16(const uint8_t* p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
inline uint16_t rd_u16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
inline int32_t  rd_i32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
inline uint32_t rd_u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

} // namespace

std::unique_ptr<c_h2_bitmap_font> c_h2_bitmap_font::load(const std::wstring& path) {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) return nullptr;
	std::streamsize sz = f.tellg();
	if (sz < 0x40400) return nullptr;
	f.seekg(0, std::ios::beg);
	std::vector<uint8_t> buf((size_t)sz);
	f.read(reinterpret_cast<char*>(buf.data()), sz);
	if (!f) return nullptr;

	// Header at 0x200.
	const uint8_t* h = buf.data() + 0x200;
	uint32_t magic = rd_u32(h);
	if (magic != 0xF0000001u) {
		CONSOLE_LOG_WARN("h2_bitmap_font: bad magic 0x%08X at +0x200 (expected 0xF0000001) in %ls",
			magic, path.c_str());
		return nullptr;
	}

	auto font = std::make_unique<c_h2_bitmap_font>();
	font->m_ascend  = rd_i16(h + 0x04);
	font->m_descend = rd_i16(h + 0x06);
	// LeadHeight (h + 0x08) and LeadWidth (h + 0x0A) ignored; halox doesn't use
	// these (line-spacing comes from caller-provided metrics in set_selection).
	int32_t character_count = rd_i32(h + 0x0C);
	if (character_count <= 0 || character_count > 65536) {
		CONSOLE_LOG_WARN("h2_bitmap_font: bogus character_count %d in %ls",
			character_count, path.c_str());
		return nullptr;
	}
	// Skip MaxCompressedSize, MaxDecompressedSize, CompressedSize, DecompressedSize (4×i32 = 16B at +0x10).
	int32_t pair_count = rd_i32(h + 0x20);
	if (pair_count < 0 || pair_count > 0x10000) {
		CONSOLE_LOG_WARN("h2_bitmap_font: bogus pair_count %d in %ls",
			pair_count, path.c_str());
		return nullptr;
	}

	// Kerning pairs immediately follow the header (each is 4 bytes).
	const uint8_t* kp = h + 0x24;
	if (kp + (size_t)pair_count * 4 > buf.data() + buf.size()) return nullptr;
	for (int i = 0; i < pair_count; ++i) {
		const uint8_t* e = kp + i * 4;
		uint8_t left  = e[0];
		uint8_t right = e[1];
		int16_t offset = rd_i16(e + 2);
		font->m_kerning[((uint32_t)left << 16) | right] = offset;
	}

	// Records start at fixed 0x40400. Validate file is large enough.
	const size_t records_off = 0x40400;
	if (records_off + (size_t)character_count * 0x10 > buf.size()) {
		CONSOLE_LOG_WARN("h2_bitmap_font: file too small for character records (%zu bytes, need %zu) in %ls",
			buf.size(), records_off + (size_t)character_count * 0x10, path.c_str());
		return nullptr;
	}

	// Build per-index glyph cache first; m_glyphs (keyed by Unicode) gets
	// populated in the second pass after we walk the codepoint→index map.
	struct loaded_record { int width, height, origin_x, origin_y, display_width; std::vector<uint8_t> alpha; };
	std::vector<loaded_record> records((size_t)character_count);

	for (int i = 0; i < character_count; ++i) {
		const uint8_t* r = buf.data() + records_off + (size_t)i * 0x10;
		uint16_t display_width = rd_u16(r + 0x00);
		uint16_t data_length   = rd_u16(r + 0x02);
		uint16_t width         = rd_u16(r + 0x04);
		uint16_t height        = rd_u16(r + 0x06);
		int16_t  origin_x      = rd_i16(r + 0x08);
		int16_t  origin_y      = rd_i16(r + 0x0A);
		uint32_t data_offset   = rd_u32(r + 0x0C);

		records[i].width         = width;
		records[i].height        = height;
		records[i].origin_x      = origin_x;
		records[i].origin_y      = origin_y;
		records[i].display_width = display_width;

		if (width == 0 || height == 0 || data_length == 0) continue;
		if (data_offset + data_length > buf.size()) {
			CONSOLE_LOG_WARN("h2_bitmap_font: char %d data range %u..%u out of file (%zu) in %ls",
				i, data_offset, data_offset + data_length, buf.size(), path.c_str());
			continue;
		}

		std::vector<uint8_t> bgra;
		if (!decompress_h2_glyph(buf.data() + data_offset, data_length, (size_t)width * height, bgra)) {
			// Keep what we got — partial glyph is better than blank — but log once
			// per file at most by tying the warning to character index 0.
			if (i == 0) {
				CONSOLE_LOG_WARN("h2_bitmap_font: glyph decompress mismatch (char %d, %d×%d, got %zu/%zu bytes) in %ls",
					i, (int)width, (int)height, bgra.size(), (size_t)width * height * 4, path.c_str());
			}
		}
		// Extract alpha channel (every 4th byte starting at offset 3).
		records[i].alpha.resize((size_t)width * height);
		size_t pix = std::min((size_t)width * height, bgra.size() / 4);
		for (size_t p = 0; p < pix; ++p)
			records[i].alpha[p] = bgra[p * 4 + 3];
	}

	// Codepoint→index map at 0x400 — 65536 entries × i32. Entry[0] is the
	// "missing glyph" sentinel (firstpointer); any later entry whose value
	// equals firstpointer is treated as "no glyph" and skipped.
	const uint8_t* map = buf.data() + 0x400;
	if (map + 65536 * 4 > buf.data() + buf.size()) return nullptr;
	int32_t firstpointer = rd_i32(map);
	for (int u = 0; u < 65536; ++u) {
		int32_t idx = rd_i32(map + u * 4);
		if (u != 0 && idx == firstpointer) continue;
		if (idx < 0 || idx >= character_count) continue;
		const auto& rec = records[idx];
		// Even U+0 gets stored if the file maps it to a real glyph (some files
		// dedicate it to the default-glyph; the find path will only be called
		// for actual codepoints from the game so this isn't user-visible).
		s_h2_glyph& g = font->m_glyphs[(uint16_t)u];
		g.width         = rec.width;
		g.height        = rec.height;
		g.origin_x      = rec.origin_x;
		g.origin_y      = rec.origin_y;
		g.display_width = rec.display_width;
		g.alpha         = rec.alpha;
	}

	CONSOLE_LOG_INFO("h2_bitmap_font: loaded %ls (chars=%d glyphs=%zu kerning_pairs=%d ascend=%d descend=%d)",
		path.c_str(), character_count, font->m_glyphs.size(), pair_count,
		font->m_ascend, font->m_descend);
	return font;
}

const s_h2_glyph* c_h2_bitmap_font::find_glyph(wchar_t unicode) const {
	if ((unsigned)unicode > 0xFFFF) return nullptr;
	auto it = m_glyphs.find((uint16_t)unicode);
	return (it == m_glyphs.end()) ? nullptr : &it->second;
}

int c_h2_bitmap_font::kerning(wchar_t left, wchar_t right) const {
	if ((unsigned)left > 0xFF || (unsigned)right > 0xFF) return 0;
	auto it = m_kerning.find(((uint32_t)(uint8_t)left << 16) | (uint8_t)right);
	return (it == m_kerning.end()) ? 0 : it->second;
}
