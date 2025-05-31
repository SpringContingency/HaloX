#include "import_unreal_font.h"
#include "../text/font_package.h"

#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <nlohmann/json.hpp>
#include <cassert>

using namespace std;
using namespace nlohmann;

const char* k_unreal_font_format = "ufont";
const char* k_game_font_path = "MCC/Content/UI/Shared/Fonts/GameFonts/F_GameFonts.0";

struct s_unreal_font_face_asset {
	string object_name;
	string object_path;
};

struct s_unreal_font_face {
	string name;
	const s_unreal_font_face_asset& asset;
};

struct s_unreal_typeface {
	vector<s_unreal_font_face> fonts;

	void convert(
		s_typeface& typeface,
		std::vector<s_font>& fonts,
		const unordered_map<string, int>& font_asset_map
	) {
		typeface.fonts = s_range(
			fonts.size(),
			fonts.size() + this->fonts.size()
		);

		for (auto& font : this->fonts) {
			auto& new_font = fonts.emplace_back(s_font{});
			new_font.asset_index = font_asset_map.at(font.asset.object_path);
			strncpy(new_font.name, font.name.c_str(), sizeof(new_font.name) - 1);
		}
	}
};

struct s_unreal_subtypeface {
	vector<s_character_range> character_ranges;
	s_unreal_typeface typeface;

	void convert(
		s_subtypeface& subtypeface,
		std::vector<s_font>& fonts,
		std::vector<s_character_range>& character_ranges,
		const unordered_map<string, int>& font_asset_map
	) {
		subtypeface.character_ranges = s_range(
			character_ranges.size(),
			character_ranges.size() + this->character_ranges.size()
		);

		for (auto& character_range : this->character_ranges) {
			character_ranges.emplace_back(character_range);
		}

		typeface.convert(subtypeface.typeface, fonts, font_asset_map);
	}
};

static string format_asset(const char* path, const char* format) {
	return regex_replace(path, regex(R"((.*\.)(.*))"), string("$1") + format);
}

class c_unreal_font_importer {
public:
	std::unique_ptr<s_runtime_font_package> import(const json& json) {
		std::unique_ptr<s_runtime_font_package> result(new s_runtime_font_package());

		import_properties(json.at("Properties"));

		unordered_map<string, int> font_asset_map;

		// assets
		{
			uint32_t uncompressed_data_size = 0;
			uint32_t uncompress_data_offset = 0;
			std::vector<std::vector<char>> buffers;

			buffers.reserve(m_font_face.size());

			// read font files
			for (auto& item : m_font_face) {
				auto object_path = item.second.object_path.c_str();
				auto font_path = format_asset(object_path, k_unreal_font_format);

				font_asset_map.emplace(object_path, buffers.size());

				std::ifstream file(font_path, std::ios::binary);
				file.seekg(0, std::ios::end);
				size_t data_size = file.tellg();

				std::vector<char> buffer(data_size);
				file.seekg(0, std::ios::beg);
				file.read(buffer.data(), buffer.size());

				uncompressed_data_size += data_size;

				buffers.emplace_back(std::move(buffer));
			}

			result->assets.resize(buffers.size());
			result->uncompress_data.resize(uncompressed_data_size);

			// write font files to memory buffer
			for (auto i = 0; i < buffers.size(); ++i) {
				auto& buffer = buffers[i];
				auto& asset = result->assets[i];

				asset.offset = uncompress_data_offset;
				asset.size = buffer.size();

				memcpy(
					result->uncompress_data.data() + asset.offset, 
					buffer.data(), 
					asset.size
				);

				uncompress_data_offset += asset.size;
			}
		}

		m_default_typeface.convert(result->default_typeface, result->fonts, font_asset_map);
		m_fallback_typeface.convert(result->fallback_typeface, result->fonts, font_asset_map);

		// subtypefaces
		{
			auto size = m_subtypefaces.size();
			result->subtypefaces.resize(size);

			for (int i = 0; i < size; ++i) {
				auto& subtypeface = m_subtypefaces[i];
				auto& _subtypeface = result->subtypefaces[i];

				subtypeface.convert(
					_subtypeface, 
					result->fonts, 
					result->character_ranges, 
					font_asset_map);
			}
		}
		 
		return result;
	}

private:
	int import_properties(const json& properties) {
		import_composite_font(properties.at("CompositeFont"));
		return 0;
	}

	int import_composite_font(const json& composite_font) {
		import_default_typeface(composite_font.at("DefaultTypeface"));
		import_fallback_typeface(composite_font.at("FallbackTypeface"));
		import_subtypefaces(composite_font.at("SubTypefaces"));
		return 0;
	}

	int import_default_typeface(const json& default_typeface) {
		import_typeface(default_typeface, m_default_typeface);
		return 0;
	}

	int import_fallback_typeface(const json& fallback_typeface) {
		import_typeface(fallback_typeface.at("Typeface"), m_fallback_typeface);
		return 0;
	}

	int import_subtypefaces(const json& subtypefaces) {
		for (auto& subtypeface : subtypefaces) {
			s_unreal_subtypeface _subtypeface;

			auto& character_ranges = subtypeface.at("CharacterRanges");

			for (auto& character_range : character_ranges) {
				int lower_bound = character_range.at("LowerBound").at("Value");
				int upper_bound = character_range.at("UpperBound").at("Value");
				_subtypeface.character_ranges.emplace_back(lower_bound, upper_bound + 1);
			}

			import_typeface(subtypeface.at("Typeface"), _subtypeface.typeface);

			m_subtypefaces.push_back(move(_subtypeface));
		}
		return 0;
	}

	const s_unreal_font_face_asset& add_font_face(const json& font_face) {
		auto& asset = font_face.at("LocalFontFaceAsset");
		auto& name = asset.at("ObjectName");
		auto& path = asset.at("ObjectPath");

		auto it = m_font_face.find(path);

		if (it != m_font_face.end()) {
			return it->second;
		}

		s_unreal_font_face_asset ffa{
			.object_name = name,
			.object_path = path
		};

		auto result = m_font_face.emplace(path, ffa);

		assert(result.second);

		return result.first->second;
	}

	int import_typeface(const json& json, s_unreal_typeface& typeface) {
		typeface.fonts.clear();
		for (auto& font : json.at("Fonts")) {
			typeface.fonts.emplace_back(
				font.at("Name"),
				add_font_face(font.at("Font"))
			);
		}
		return 0;
	}

	unordered_map<string, s_unreal_font_face_asset> m_font_face;
	s_unreal_typeface m_default_typeface;
	s_unreal_typeface m_fallback_typeface;
	vector<s_unreal_subtypeface> m_subtypefaces;
};

static int import_unreal_font(int argc, const char** argv) {
	if (argc != 1) {
		cerr << "Usage: import_unreal_font <root>" << endl;
		return -1;
	}

	filesystem::current_path(argv[0]);

	// import game font configuration
	auto file = format_asset(k_game_font_path, "json");
	auto json = json::parse(ifstream(file));

	for (auto& font : json) {
		auto package = c_unreal_font_importer().import(font);
		auto buffer = save_font_package(package.get());
		ofstream("font_package.bin", std::ios::binary | ios_base::out)
			.write(buffer.data(), buffer.size());
		break;
	}

	return 0;
}

s_command g_command_import_unreal_font{
	.name = "import_unreal_font",
	.description = "<root>",
	.proc = import_unreal_font
};