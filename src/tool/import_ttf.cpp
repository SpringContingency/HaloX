#include "import_ttf.h"

#include "../text/font_package.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

// import_ttf <ttf_path> <font_name> [out_path]
//
// Builds a minimum-viable HaloX font package out of a single TrueType file:
//   - one font asset (the .ttf bytes)
//   - one s_font naming it
//   - default + fallback typeface both pointing at that font
//   - one Latin character range so the typeface chain has something to match
//
// Writes to <out_path> (defaults to "font_package.bin" in cwd).
static int import_ttf(int argc, const char** argv) {
	if (argc < 2) {
		printf("Usage: import_ttf <ttf_path> <font_name> [out_path]\n");
		return 1;
	}

	const char* ttf_path  = argv[0];
	const char* font_name = argv[1];
	const char* out_path  = (argc >= 3) ? argv[2] : "font_package.bin";

	std::ifstream in(ttf_path, std::ios::binary);
	if (!in) {
		printf("Failed to open %s\n", ttf_path);
		return 2;
	}
	std::vector<char> ttf_data((std::istreambuf_iterator<char>(in)),
	                            std::istreambuf_iterator<char>());
	if (ttf_data.empty()) {
		printf("%s is empty\n", ttf_path);
		return 3;
	}

	s_runtime_font_package pkg{};

	// One asset: the raw TTF bytes.
	s_font_asset asset{};
	asset.size   = (uint32_t)ttf_data.size();
	asset.offset = 0;
	pkg.assets.push_back(asset);

	// One font referencing the asset.
	s_font font{};
	std::strncpy(font.name, font_name, sizeof(font.name) - 1);
	font.asset_index = 0;
	pkg.fonts.push_back(font);

	// Default + fallback typeface both contain just font index 0.
	pkg.default_typeface.fonts  = s_range((uint8_t)0, (uint8_t)1);
	pkg.fallback_typeface.fonts = s_range((uint8_t)0, (uint8_t)1);

	// One Latin character range (0x20..0x7F).
	pkg.character_ranges.push_back(s_character_range((wchar_t)0x20, (wchar_t)0x7F));

	pkg.uncompress_data = std::move(ttf_data);

	auto serialized = save_font_package(&pkg);

	std::ofstream out(out_path, std::ios::binary);
	if (!out) {
		printf("Failed to open %s for write\n", out_path);
		return 4;
	}
	out.write(serialized.data(), serialized.size());
	out.close();

	printf("Wrote %s (%zu bytes) from %s as font \"%s\"\n",
		out_path, serialized.size(), ttf_path, font_name);
	return 0;
}

s_command g_command_import_ttf{
	.name = "import_ttf",
	.description = "<ttf_path> <font_name> [out_path]  -- build a minimal font_package.bin",
	.proc = import_ttf,
};
