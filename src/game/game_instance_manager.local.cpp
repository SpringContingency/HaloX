#include "game_instance_manager.h"

#include "../logging/logging.h"

#include <Windows.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <string>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

using namespace libmcc;

// Resolve an MCC content root we can read variants from. We try in order:
//   1. $HALOX_MCC_ROOT       — explicit user override
//   2. Steam library default (Program Files (x86)/Steam/...)
//   3. Common Steam library drive paths (D:\, E:\, F:\)
// Cached after the first call. Returns nullptr if no candidate exists.
const wchar_t* resolve_mcc_root_impl();
extern "C" const wchar_t* halox_resolve_mcc_root() { return resolve_mcc_root_impl(); }

const wchar_t* resolve_mcc_root_impl() {
	static std::wstring cached;
	static bool resolved = false;
	if (resolved) return cached.empty() ? nullptr : cached.c_str();
	resolved = true;

	auto looks_like_mcc = [](const wchar_t* dir) -> bool {
		// Sanity check: real MCC roots have a halo3 subdirectory.
		std::wstring probe = dir;
		probe += L"\\halo3";
		DWORD attrs = GetFileAttributesW(probe.c_str());
		return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
	};

	wchar_t buf[MAX_PATH];
	DWORD len = GetEnvironmentVariableW(L"HALOX_MCC_ROOT", buf, MAX_PATH);
	if (len > 0 && len < MAX_PATH && looks_like_mcc(buf)) {
		cached = buf;
		return cached.c_str();
	}

	static const wchar_t* candidates[] = {
		L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Halo The Master Chief Collection",
		L"D:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection",
		L"D:\\Steam\\steamapps\\common\\Halo The Master Chief Collection",
		L"E:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection",
		L"E:\\Steam\\steamapps\\common\\Halo The Master Chief Collection",
		L"F:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection",
		L"G:\\SteamLibrary\\steamapps\\common\\Halo The Master Chief Collection",
	};
	for (auto* c : candidates) {
		if (looks_like_mcc(c)) {
			cached = c;
			return cached.c_str();
		}
	}

	return nullptr;
}

// Read an mvar file and pull out its built-in map id. Walks BLF chunks (each:
// 4 magic + 4 size BE + 2 ver BE + 2 flags BE + 20 sha1 + 4 contentLen BE +
// payload), finds "mvar", then bit-decodes the GameVariantHeader far enough
// to reach the 32-bit MapId field at bit offset 301. Bits are MSB-first
// within each byte (matches MVARStudio's BitstreamReader). Returns -1 on any
// failure (missing/truncated/no mvar chunk).
static int read_mvar_map_id(const wchar_t* path) {
	// mvar chunk header (with payload up to MapId) sits ~800 bytes into the
	// file. Read just a small prefix so 1000+ files don't take seconds at
	// startup. FILE_FLAG_SEQUENTIAL_SCAN hints to the OS not to populate the
	// whole-file cache.
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
	                       OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
	if (h == INVALID_HANDLE_VALUE) return -1;

	const DWORD k_prefix = 2048;
	uint8_t prefix[k_prefix];
	DWORD got = 0;
	BOOL ok = ReadFile(h, prefix, k_prefix, &got, nullptr);
	CloseHandle(h);
	if (!ok || got < 36) return -1;

	const uint8_t* buf_data = prefix;
	DWORD fsize = got;

	auto be32 = [](const uint8_t* p) {
		return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
		       (uint32_t)p[2] << 8  | (uint32_t)p[3];
	};

	uint32_t off = 0;
	while (off + 12 <= fsize) {
		const uint8_t* hp = buf_data + off;
		uint32_t csize = be32(hp + 4);
		if (csize < 12) break;

		if (memcmp(hp, "mvar", 4) == 0) {
			// We only read a 2 KB prefix, so an mvar chunk's full csize will
			// almost certainly extend past what we have — that's fine. Just
			// require the bytes through MapId (header 36 + ~42 of payload).
			const size_t k_need = 36 + 42;
			if (off + k_need > fsize) return -1;
			const uint8_t* payload = hp + 36;
			size_t psize = (size_t)fsize - (off + 36);

			// Bit position of MapId field within payload:
			//   4 (Type) + 32 (FileLength) + 64*4 (Unk08..Unk20)
			//   + 3 (Activity) + 3 (GameMode) + 3 (Engine) = 301 bits.
			const size_t map_id_bit = 301;
			if (map_id_bit + 32 > psize * 8) return -1;

			uint32_t mid = 0;
			for (size_t i = 0; i < 32; ++i) {
				size_t bit_pos = map_id_bit + i;
				size_t byte_idx = bit_pos >> 3;
				int bit_idx = 7 - (int)(bit_pos & 7);
				uint32_t b = (payload[byte_idx] >> bit_idx) & 1;
				mid = (mid << 1) | b;
			}
			return (int)mid;
		}

		// Past-prefix walks are bounded by what we read. Bail when the next
		// chunk would extend beyond it; mvar should have appeared by then.
		if ((uint64_t)off + csize > fsize) break;
		off += csize;
	}
	return -1;
}

// Enumerate variant files in `dir` matching `pattern` (e.g. "*.bin"). Returns
// true if at least one file was found. Stores filenames in `out`.
static bool enumerate_variants(const wchar_t* dir, const wchar_t* pattern,
                               std::vector<std::string>* out) {
	wchar_t buffer[1024];
	swprintf(buffer, 1024, L"%s\\%s", dir, pattern);

	WIN32_FIND_DATAW find;
	HANDLE handle = FindFirstFileW(buffer, &find);
	if (handle == INVALID_HANDLE_VALUE) return false;

	do {
		// Convert filename UTF-16 → UTF-8 for storage (matches the existing
		// std::string vector type and is fine for ASCII filenames; full UTF-8
		// path is reconstructed in get_saved_game_file from game_variant_dir).
		char utf8[MAX_PATH];
		int n = WideCharToMultiByte(CP_UTF8, 0, find.cFileName, -1,
		                            utf8, sizeof(utf8), nullptr, nullptr);
		if (n > 0) out->emplace_back(utf8);
	} while (FindNextFileW(handle, &find));
	FindClose(handle);
	return !out->empty();
}

int c_game_instance_manager::load_local() {
	const wchar_t* mcc_root = resolve_mcc_root_impl();
	if (mcc_root) {
		CONSOLE_LOG_INFO("MCC content root: %ls", mcc_root);
	} else {
		CONSOLE_LOG_INFO("MCC content root: not found (set HALOX_MCC_ROOT env var to override). "
			"Variants will only be visible if the game subdirs are junctioned next to halox.exe.");
	}

	for (int i = _module_halo1; i < k_game_count; ++i) {
		auto local = s_game_locals + i;

		local->hopper_game_variants.clear();
		local->hopper_map_variants.clear();
		local->hopper_map_variant_ids.clear();
		local->game_variant_dir.clear();
		local->map_variant_dir.clear();

		// Don't gate variant enumeration on DLL load — variants are content
		// files. We want the dropdowns populated even before a successful
		// DLL load so users can see what's available; launch will fail with
		// a clear "data_access null" if the DLL really isn't there.

		auto title = k_game_names[i];
		wchar_t wtitle[64];
		MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 64);

		// Try cwd-relative first (junctioned setups), then MCC root.
		auto try_dir = [](const std::wstring& dir, const wchar_t* pattern,
		                  std::vector<std::string>* out, std::wstring* out_dir) -> bool {
			if (enumerate_variants(dir.c_str(), pattern, out)) {
				*out_dir = dir;
				return true;
			}
			return false;
		};

		// Game variants
		{
			std::wstring cwd_dir = std::wstring(L".\\") + wtitle + L"\\hopper_game_variants";
			if (!try_dir(cwd_dir, L"*.bin", &local->hopper_game_variants, &local->game_variant_dir) && mcc_root) {
				std::wstring mcc_dir = std::wstring(mcc_root) + L"\\" + wtitle + L"\\hopper_game_variants";
				try_dir(mcc_dir, L"*.bin", &local->hopper_game_variants, &local->game_variant_dir);
			}
		}

		// Map variants
		{
			std::wstring cwd_dir = std::wstring(L".\\") + wtitle + L"\\hopper_map_variants";
			if (!try_dir(cwd_dir, L"*.mvar", &local->hopper_map_variants, &local->map_variant_dir) && mcc_root) {
				std::wstring mcc_dir = std::wstring(mcc_root) + L"\\" + wtitle + L"\\hopper_map_variants";
				try_dir(mcc_dir, L"*.mvar", &local->hopper_map_variants, &local->map_variant_dir);
			}
		}

		// Index each mvar's built-in map id by reading its BLF/bitstream header
		// directly. Lets the launcher dropdown filter map variants against the
		// selected map. -1 means we couldn't parse it (truncated/foreign); the
		// UI treats those as map-agnostic so a parse miss never hides a file.
		local->hopper_map_variant_ids.assign(local->hopper_map_variants.size(), -1);
		int indexed = 0;
		if (!local->hopper_map_variants.empty() && !local->map_variant_dir.empty()) {
			for (size_t mi = 0; mi < local->hopper_map_variants.size(); ++mi) {
				wchar_t fpath[1024];
				swprintf(fpath, 1024, L"%s\\%hs",
					local->map_variant_dir.c_str(),
					local->hopper_map_variants[mi].c_str());
				int id = read_mvar_map_id(fpath);
				local->hopper_map_variant_ids[mi] = id;
				if (id != -1) indexed++;
			}
		}

		CONSOLE_LOG_INFO(
			"  %-9s game_variants=%zu map_variants=%zu (indexed %d/%zu)",
			title,
			local->hopper_game_variants.size(),
			local->hopper_map_variants.size(),
			indexed,
			local->hopper_map_variants.size());
	}

	return 0;
}

i_unknown_ptr<i_unknown> c_game_instance_manager::get_saved_game_file(
	e_module game,
	int game_file_type,
	int game_file_index
) {
	int status;
	HANDLE handle;
	i_unknown* result;
	wchar_t buffer[1024];
	auto local = s_game_locals + game;
	auto variants = game_file_type ? &local->hopper_map_variants : &local->hopper_game_variants;
	const std::wstring& dir = game_file_type ? local->map_variant_dir : local->game_variant_dir;

	if (game_file_index < 0 || game_file_index >= (int)variants->size()) {
		return nullptr;
	}
	if (dir.empty()) {
		CONSOLE_LOG_WARN("get_saved_game_file: no resolved directory for %s/%s",
			k_game_names[game],
			game_file_type ? "hopper_map_variants" : "hopper_game_variants");
		return nullptr;
	}

	auto data_access = get_module_data_access(game);
	if (!data_access) {
		return nullptr;
	}

	auto& name = variants->at(game_file_index);
	swprintf(buffer, 1024, L"%s\\%hs", dir.c_str(), name.c_str());

	handle = CreateFileW(
		buffer,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) {
		CONSOLE_LOG_WARN("get_saved_game_file: open failed (%lu) %ls",
			GetLastError(), buffer);
		return nullptr;
	}

	auto file_size = GetFileSize(handle, nullptr);
	if (file_size == INVALID_FILE_SIZE) {
		CloseHandle(handle);
		return nullptr;
	}

	std::vector<char> file_data(file_size);
	status = ReadFile(handle, file_data.data(), file_size, nullptr, nullptr);
	CloseHandle(handle);

	if (status == 0) {
		return nullptr;
	}

	if (game_file_type) {
		result = data_access->create_scenario_map_variant_from_file(
			file_data.data(),
			file_size
		);
	} else {
		result = data_access->create_game_engine_variant_from_file(
			file_data.data(),
			file_size
		);
	}

	return i_unknown_ptr<i_unknown>(result);
}

i_unknown_ptr<i_game_engine_variant> c_game_instance_manager::get_game_variant(
	e_module game,
	int hopper_game_variant
) {
	auto result = get_saved_game_file(game, 0, hopper_game_variant).release();
	return i_unknown_ptr<i_game_engine_variant>(reinterpret_cast<i_game_engine_variant*>(result));
}

i_unknown_ptr<i_scenario_map_variant> c_game_instance_manager::get_map_variant(
	e_module game,
	int hopper_map_variant
) {
	auto result = get_saved_game_file(game, 1, hopper_map_variant).release();
	return i_unknown_ptr<i_scenario_map_variant>(reinterpret_cast<i_scenario_map_variant*>(result));
}
