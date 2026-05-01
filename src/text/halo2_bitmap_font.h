#pragma once

// Loader for MCC's halo2-era extensionless bitmap font files
// (`<MCC>/halo2/h2_fonts/conduit-13`, `handel_gothic-24`, ...).
//
// File format (per Lord-Zedd/FontPackager Classes/TableIO.cs):
//   0x200: u32 magic = 0xF0000001 (FontVersionLoose)
//   0x204: i16 ascend, i16 descend, i16 lead_h, i16 lead_w
//   0x20C: i32 character_count
//   0x210: i32 max_compressed_size, max_decompressed_size, compressed_size, decompressed_size
//   0x220: i32 kerning_pair_count, then count × { u8 left, u8 right, i16 offset }
//          followed by 8 × i32 "kerning bits" lookup table
//   0x400: 65536 × i32 — Unicode codepoint → character index map.
//          Entry at U+0000 is the "missing-glyph sentinel" — any later entry
//          whose value equals it has no real glyph and is skipped.
//   0x40400: character_count × 0x10-byte records:
//          { u16 display_width, u16 data_length, u16 width, u16 height,
//            i16 origin_x, i16 origin_y, u32 data_offset (absolute file offset) }
//   <data_offset>: per-character compressed bitmap bytes (length = data_length).
//
// Compressed bitmap stream (per Classes/CharacterTools.cs DecompressData):
//   custom RLE producing BGRA32 pixels; we extract the alpha channel only
//   since halox's glyph atlas texture is DXGI_FORMAT_A8_UNORM.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct s_h2_glyph {
	int width;          // bitmap width in pixels
	int height;         // bitmap height in pixels
	int origin_x;       // bearing X — pen offset to glyph left edge
	int origin_y;       // bearing Y — baseline-to-top distance
	int display_width;  // horizontal advance
	std::vector<uint8_t> alpha;  // width * height bytes (8-bit alpha)
};

class c_h2_bitmap_font {
public:
	// Loads a single h2_fonts file. Returns nullptr on parse error.
	static std::unique_ptr<c_h2_bitmap_font> load(const std::wstring& path);

	int ascend()  const { return m_ascend; }
	int descend() const { return m_descend; }
	int height()  const { return m_ascend + m_descend; }

	// Design size — the number in the filename (e.g. conduit-13 → 13).
	// This is the scale unit MCC's engine uses to relate per-size atlases
	// to engine `size` requests. AscendHeight/DescendHeight in the file
	// header are the bitmap pixel extents (always larger than the design
	// size because typefaces have ascenders), so they're NOT the right
	// scale denominator. Set by find_or_load_h2_bitmap after `load()`.
	int  design_size() const { return m_design_size; }
	void set_design_size(int v) { m_design_size = v; }

	// Returns nullptr if the codepoint has no glyph in this file.
	const s_h2_glyph* find_glyph(wchar_t unicode) const;

	// Returns kerning offset (in pixels) for (left, right) or 0 if no entry.
	int kerning(wchar_t left, wchar_t right) const;

private:
	int m_ascend = 0;
	int m_descend = 0;
	int m_design_size = 0;
	std::unordered_map<uint16_t, s_h2_glyph> m_glyphs;
	std::unordered_map<uint32_t, int> m_kerning;  // (left<<16|right) -> offset
};
