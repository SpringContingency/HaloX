#include "font_package.h"

#include <zlib.h>

enum e_font_package_directory_entry {
	_font_package_directory_entry_global,
	_font_package_directory_entry_subtypefaces,

	_font_package_directory_entry_fonts,
	_font_package_directory_entry_assets,
	_font_package_directory_entry_character_ranges,
	_font_package_directory_entry_compressed_data,

	k_font_package_directory_entry_count
};



struct s_font_package_directory_entry {
	uint32_t offset;
	uint32_t size;
};

struct s_font_package_directory {
	s_font_package_directory_entry entries[k_font_package_directory_entry_count];
};

struct s_font_package {
	s_font_package_directory directory;
};

struct s_font_package_directory_global {
	s_typeface default_typeface;
	s_typeface fallback_typeface;
	uint32_t number_of_subtypefaces;
	uint32_t number_of_fonts;
	uint32_t number_of_assets;
	uint32_t number_of_character_ranges;
	uint32_t size_of_uncompressed_data;
};

typedef s_subtypeface s_font_package_subtypefaces;
typedef s_font s_font_package_fonts;
typedef s_font_asset s_font_package_assets;
typedef s_character_range s_font_package_character_ranges;
typedef char s_font_package_compressed_data;

std::vector<char> save_font_package(const s_runtime_font_package* package) {
	std::vector<char> result;
	std::vector<char> compressed_data;
	s_font_package_directory directory;

	s_font_package_directory_global global{
		.default_typeface = package->default_typeface,
		.fallback_typeface = package->fallback_typeface,
		.number_of_subtypefaces = static_cast<uint32_t>(package->subtypefaces.size()),
		.number_of_fonts = static_cast<uint32_t>(package->fonts.size()),
		.number_of_assets = static_cast<uint32_t>(package->assets.size()),
		.number_of_character_ranges = static_cast<uint32_t>(package->character_ranges.size()),
		.size_of_uncompressed_data = static_cast<uint32_t>(package->uncompress_data.size())
	};

	// calculate size of compressed data
	{
		auto size = package->uncompress_data.size();
		auto data = package->uncompress_data.data();
		auto compressed_data_size = compressBound(size);

		compressed_data.resize(compressed_data_size);

		int result = compress(
			reinterpret_cast<Bytef*>(compressed_data.data()),
			&compressed_data_size,
			reinterpret_cast<const Bytef*>(data),
			size
		);

		if (result != Z_OK) {
			return {};
		}

		compressed_data.resize(compressed_data_size);
	}

	size_t total_size = 
		sizeof(s_font_package_directory) +
		sizeof(s_font_package_directory_global) +
		sizeof(s_font_package_subtypefaces) * global.number_of_subtypefaces +
		sizeof(s_font_package_fonts) * global.number_of_fonts +
		sizeof(s_font_package_assets) * global.number_of_assets +
		sizeof(s_font_package_character_ranges) * global.number_of_character_ranges +
		sizeof(s_font_package_compressed_data) * compressed_data.size();

	result.resize(total_size);

	auto offset = sizeof(s_font_package_directory);

	for (int i = 0; i < k_font_package_directory_entry_count; ++i) {
		const void* data = nullptr;
		auto entry = directory.entries + i;
		entry->offset = offset;

		switch (i) {
		case _font_package_directory_entry_global:
			data = &global;
			entry->size = sizeof(s_font_package_directory_global);
			break;
		case _font_package_directory_entry_subtypefaces:
			data = package->subtypefaces.data();
			entry->size = sizeof(s_font_package_subtypefaces) * global.number_of_subtypefaces;
			break;
		case _font_package_directory_entry_fonts:
			data = package->fonts.data();
			entry->size = sizeof(s_font_package_fonts) * global.number_of_fonts;
			break;
		case _font_package_directory_entry_assets:
			data = package->assets.data();
			entry->size = sizeof(s_font_package_assets) * global.number_of_assets;
			break;
		case _font_package_directory_entry_character_ranges:
			data = package->character_ranges.data();
			entry->size = sizeof(s_font_package_character_ranges) * global.number_of_character_ranges;
			break;
		case _font_package_directory_entry_compressed_data:
			data = compressed_data.data();
			entry->size = sizeof(s_font_package_compressed_data) * compressed_data.size();
			break;
		default:
			return {};
		}

		memcpy(result.data() + offset, data, entry->size);

		offset += entry->size;
	}

	memcpy(result.data(), &directory, sizeof(s_font_package_directory));

	return result;
}

std::unique_ptr<s_runtime_font_package> load_font_package(const char* buf, size_t len) {
	std::vector<char> uncompress_data;
	std::unique_ptr<s_runtime_font_package> result(new s_runtime_font_package);

	if (len < sizeof(s_font_package_directory)) {
		return nullptr;
	}

	auto directory = reinterpret_cast<const s_font_package_directory*>(buf);

	// decompress data
	{
		auto global_entry = directory->entries + _font_package_directory_entry_global;

		if (0ll + global_entry->offset + global_entry->size > len) {
			return nullptr;
		}

		auto data_entry = directory->entries + _font_package_directory_entry_compressed_data;

		if (0ll + data_entry->offset + data_entry->size > len) {
			return nullptr;
		}

		auto compressed_data = reinterpret_cast<const char*>(buf + data_entry->offset);
		auto compressed_data_size = data_entry->size;
		auto global = reinterpret_cast<const s_font_package_directory_global*>(buf + global_entry->offset);
		uLong uncompressed_data_size = global->size_of_uncompressed_data;

		uncompress_data.resize(uncompressed_data_size);

		int result = uncompress(
			reinterpret_cast<Bytef*>(uncompress_data.data()),
			&uncompressed_data_size,
			reinterpret_cast<const Bytef*>(compressed_data),
			compressed_data_size
		);

		if (result != Z_OK) {
			return nullptr;
		}

		uncompress_data.resize(uncompressed_data_size);
	}

	for (int i = 0; i < k_font_package_directory_entry_count; ++i) {
		auto entry = directory->entries + i;

		if (0ll + entry->offset + entry->size > len) {
			return nullptr;
		}

		void* dst = nullptr;
		const void* src = reinterpret_cast<const void*>(buf + entry->offset);
		size_t size = entry->size;

		switch (i) {
		case _font_package_directory_entry_global: {
			auto global = reinterpret_cast<const s_font_package_directory_global*>(src);
			result->default_typeface = global->default_typeface;
			result->fallback_typeface = global->fallback_typeface;

			result->subtypefaces.resize(global->number_of_subtypefaces);
			result->fonts.resize(global->number_of_fonts);
			result->assets.resize(global->number_of_assets);
			result->character_ranges.resize(global->number_of_character_ranges);
			continue;
		}
		case _font_package_directory_entry_subtypefaces:
			dst = result->subtypefaces.data();
			break;
		case _font_package_directory_entry_fonts:
			dst = result->fonts.data();
			break;
		case _font_package_directory_entry_assets:
			dst = result->assets.data();
			break;
		case _font_package_directory_entry_character_ranges:
			dst = result->character_ranges.data();
			break;
		case _font_package_directory_entry_compressed_data:
			result->uncompress_data = std::move(uncompress_data);
			continue;
		default:
			return nullptr;
		}

		memcpy(dst, src, size);
	}

	return result;
}
