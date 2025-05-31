#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <ranges>

typedef std::ranges::iota_view<uint8_t, uint8_t> s_range;
typedef std::ranges::iota_view<wchar_t, wchar_t> s_character_range;

struct s_font_asset {
	uint32_t size;
	uint32_t offset;
};

struct s_font {
	char name[0x1F];
	int8_t asset_index;
};

struct s_typeface {
	s_range fonts;
};

struct s_subtypeface {
	s_range character_ranges;
	s_typeface typeface;
};

struct s_runtime_font_package {
	s_typeface default_typeface;
	s_typeface fallback_typeface;
	std::vector<s_subtypeface> subtypefaces;

	std::vector<s_font> fonts;
	std::vector<s_font_asset> assets;
	std::vector<s_character_range> character_ranges;
	std::vector<char> uncompress_data;
};

std::vector<char> save_font_package(const s_runtime_font_package* package);
std::unique_ptr<s_runtime_font_package> load_font_package(const char* buf, size_t len);
