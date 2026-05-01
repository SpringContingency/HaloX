#pragma once

// =============================================================================
// game_options_layout.h — annotated names for libmcc::s_game_options
//
// libmcc/game/game_options.h is a read-only third-party header with many fields
// named un_X, padding_1, and anonymous int:32 bitfields. This file provides:
//   1. Documentation of what each "unknown" field actually holds (RE'd from
//      MCC-Win64-Shipping.exe's SessionJoinDescriptor_Init at RVA 0x1C96FC and
//      readers in haloreach/halo3 — see GAME_OPTIONS_STRUCT_NOTES.md).
//   2. Inline accessors with proper names so call sites can use sane spelling
//      without forking libmcc upstream.
//
// The struct is owned by MCC.exe — every engine (haloreach/halo3/halo4/halo2/
// halo1) just memcpy's the 0x2BF30-byte buffer into its own DAT slot. Layout
// is identical across engines; only consumer interpretation differs.
// =============================================================================

#include <libmcc/libmcc.h>

#include <cstdint>

namespace halox {

// -----------------------------------------------------------------------------
// Extended flag bits — libmcc's e_game_options_flags only declares
// _multiplayer (3) and _debug (9). RE shows additional bits in active use.
// -----------------------------------------------------------------------------
enum e_game_options_flags_ext : int {
	_game_options_flag_multiplayer            = 3,   // 0x0008  online MP path
	_game_options_flag_local_multiplayer      = 4,   // 0x0010  split-screen / local-MP active
	_game_options_flag_input_locked           = 5,   // 0x0020  MCC sets when input is gated (load/pause/etc.)
	_game_options_flag_is_listen_server       = 6,   // 0x0040  listen-server/host. (flags & 0x48)==0x48 ⇒ "online MP host"
	_game_options_flag_engine_root_override   = 7,   // 0x0080  enables un_7 (saved_film_root_path) usage
	_game_options_flag_debug                  = 9,   // 0x0200  enable init.txt + terminal
	// Bits 9..13 are also packed into a 5-bit nibble by halo3's FUN_18000A810
	// for a variant-config setter. Not used the same way in haloreach.
	_game_options_flag_saved_film_replay      = 14,  // 0x4000  saved-film / replay mode (gates saved_film_state_byte). (was non_mode_8)
	_game_options_flag_platform_guard         = 15,  // 0x8000  MCC option(0x7D) gate
};

// -----------------------------------------------------------------------------
// Annotated field accessors. Each maps a libmcc un_X / anonymous field to its
// reverse-engineered semantic name. Use these in halox code instead of the
// raw libmcc fields when the meaning matters.
//
// HIGH confidence (verified setter + reader on real game flow):
//   - option_2  (+0x03) → max_player_count            ((value-1) & 0xFF) < 0x10 range check
//   - option_3  (+0x04) → team_count                  EnqueueGameVariantUpdateEvent gate
//   - game_tick (+0x0A) → simulation tick rate (60Hz default when 0; halo3 reader)
//   - un_0 (+0x0B)      → peer_count_threshold       halo2 readers compare against live peer count
//   - +0x40 bf          → scripted_event_permission   HUD broadcast / talk-permission gate
//   - +0x44 short       → spops_campaign_variant_index  halo2-only: signed index into 51-entry
//                                                        spops scenario table; -1 for non-spops
//   - host_secure_address       (un_2  / +0x50)
//   - local_network_id          (un_3  / +0x2F8)
//   - game_variant_custom_data  (un_4  / +0x1CF00) — 1024-byte MSF blob magic 'msf_'
//   - map_variant_custom_data   (un_5  / +0x2BB08) — same MSF format (halo2 reads directly)
//   - saved_game_state_size     (un_6  / +0x2BF10) — paired sized buffer with saved_game_state
//   - saved_film_root_path      (un_7  / +0x2BF28) — UTF-16 dir prefix; gated by flags & 0x80
//   - campaign_insertion_point  (+0x3C, ProcessPlaybackFrame evidence)
//
// MEDIUM confidence:
//   - network_option_1    → mp_simulation_client_count
//   - network_option_2    → sp_simulation_client_count
//
// LOW (no readers found across haloreach/halo3/halo1/halo2):
//   - +0x38 bitfield, option_4, option_5, option_6, option_1
//   (Anniversary-graphics toggle is NOT in this struct — confirmed across all 4 engines.)
// -----------------------------------------------------------------------------

// option_2 (+0x03) — max player count. Engine validates `(byte)(value-1) < 0x10`,
// i.e. range [1..16]. Init writes 0x10 (= 16, max).
inline uint8_t& max_player_count(libmcc::s_game_options& o) { return reinterpret_cast<uint8_t&>(o.option_2); }
inline uint8_t  max_player_count(const libmcc::s_game_options& o) { return (uint8_t)o.option_2; }

// option_3 (+0x04) — team count. Used by EnqueueGameVariantUpdateEvent and the
// team-id rewrite loop in FUN_1803915a8. Init writes 0x03.
inline uint8_t& team_count(libmcc::s_game_options& o) { return reinterpret_cast<uint8_t&>(o.option_3); }
inline uint8_t  team_count(const libmcc::s_game_options& o) { return (uint8_t)o.option_3; }

// +0x44 short — per-engine variant slot ID. Same field, different per-engine
// interpretation:
//   halo2: signed index into 51-entry SPOPS scenario table
//          (PTR_s_e1_m1_chopperbowl_vehicle_destro_180c98b40). -1 for non-spops.
//          Builds "%sspops_variants\\%s_132.bin".
//   halo4: 0..50 gametype variant slot (3 readers in variant launchers)
//   halo1/halo3/haloreach: no readers — leave at -1.
struct s_game_options_layout_offsets {
	static constexpr size_t variant_slot_id = 0x44;
};
inline int16_t& variant_slot_id(libmcc::s_game_options& o) {
	return *reinterpret_cast<int16_t*>(
		reinterpret_cast<uint8_t*>(&o) + s_game_options_layout_offsets::variant_slot_id);
}
inline int16_t variant_slot_id(const libmcc::s_game_options& o) {
	return *reinterpret_cast<const int16_t*>(
		reinterpret_cast<const uint8_t*>(&o) + s_game_options_layout_offsets::variant_slot_id);
}

// Host's encoded NAT/secure address (m_HostSAddr in Bungie-era code).
// Reach copies this into DAT_180CFB5E8 only when the multiplayer flag is set.
inline uint64_t& host_secure_address(libmcc::s_game_options& o) { return reinterpret_cast<uint64_t&>(o.un_2); }
inline uint64_t  host_secure_address(const libmcc::s_game_options& o) { return reinterpret_cast<const uint64_t&>(o.un_2); }

// Local machine's network ID (caller-side identifier, NOT m_HostSAddr).
// Written by MCC's Session_PopulateJoinMetadata via Network_GetNetworkIDFromIdentifier.
// (libmcc's `// m_HostSAddr?` comment on un_3 is incorrect — m_HostSAddr is un_2.)
inline uint64_t& local_network_id(libmcc::s_game_options& o) { return reinterpret_cast<uint64_t&>(o.un_3); }
inline uint64_t  local_network_id(const libmcc::s_game_options& o) { return reinterpret_cast<const uint64_t&>(o.un_3); }

// Engine state-machine byte. Written by MCC's CoreSceneStateMachine_Update
// from engine_context+0x2C118 (a per-frame engine-state flag).
// NOTE: un_0 is char-sized in libmcc; this accessor exposes it as int8_t.

// Custom-data block layout (1032 bytes):
//   +0x000 valid          (1 = MSF blob populated)
//   +0x001 pad[7]         (alignment)
//   +0x008 magic          ('msf_' = 0x5F66736D)
//   +0x00C length         (typical 0x1D0)
//   +0x010 version        (0x00010001)
//   +0x014 payload[]      (variant-specific bytes)
struct s_variant_custom_data {
	uint8_t  valid;
	uint8_t  reserved[7];
	uint32_t magic;          // 'msf_' = 0x5F66736D
	uint32_t length;
	uint32_t version;
	uint8_t  payload[1024 - 12];
};
static_assert(sizeof(s_variant_custom_data) == 1032,
	"s_variant_custom_data must match s_game_options_unknown size");

// Per-game-variant custom data (rules / scoring / weapon overrides).
inline s_variant_custom_data& game_variant_custom_data(libmcc::s_game_options& o) {
	return reinterpret_cast<s_variant_custom_data&>(o.un_4);
}
inline const s_variant_custom_data& game_variant_custom_data(const libmcc::s_game_options& o) {
	return reinterpret_cast<const s_variant_custom_data&>(o.un_4);
}

// Per-map-variant custom data (forge-mode metadata / object placements tail).
inline s_variant_custom_data& map_variant_custom_data(libmcc::s_game_options& o) {
	return reinterpret_cast<s_variant_custom_data&>(o.un_5);
}
inline const s_variant_custom_data& map_variant_custom_data(const libmcc::s_game_options& o) {
	return reinterpret_cast<const s_variant_custom_data&>(o.un_5);
}

// un_6 (+0x2BF10) — sized-buffer pairing with saved_game_state (+0x2BF18).
// Engine reads both as a (ptr, size) tuple. Valid sizes: 0x1EC28 (gamestate.hdr)
// or 0xA60000 (mmiof.bmf).
inline uint32_t& saved_game_state_size(libmcc::s_game_options& o) { return reinterpret_cast<uint32_t&>(o.un_6); }
inline uint32_t  saved_game_state_size(const libmcc::s_game_options& o) { return reinterpret_cast<const uint32_t&>(o.un_6); }

// un_7 (+0x2BF28) — UTF-16 directory prefix. Engine appends `\haloreach\` (or
// per-engine subdir) to form a full theater root path. Gated by flags bit 7
// (engine_root_override). NOT a metadata handle as Round 1 guessed.
inline const wchar_t*& saved_film_root_path(libmcc::s_game_options& o) {
	return reinterpret_cast<const wchar_t*&>(o.un_7);
}
inline const wchar_t* saved_film_root_path(const libmcc::s_game_options& o) {
	return reinterpret_cast<const wchar_t*&>(const_cast<libmcc::s_game_options&>(o).un_7);
}

// -----------------------------------------------------------------------------
// s_game_player_options accessors (Round 7 — all 4 unknowns decoded)
// 32 bytes per slot; 16 slots @ s_player_options.player_options[].
//
//   +0x00  XUID xuid
//   +0x08  uint64_t address
//   +0x10  int unknown_2 → preferred_team_index    [HIGH] 3-engine reader match
//   +0x14  uint8_t unknown_3 → local_player_count  [MED]  written by MCC; no engine reader
//   +0x18  int unknown_4 → peer_machine_index      [HIGH] 3-engine reader match
//   +0x1C  int unknown_5 → local_controller_index  [HIGH] paired with peer_machine_index
// -----------------------------------------------------------------------------

// +0x10 — preferred team index. Range-checked [0, team_count). -1 = no
// preference. Engine xuid-match scans for the slot, then writes the result
// into the player's team-id byte. Verified: haloreach FUN_1803915A8,
// halo3 FUN_18001596C, halo2 FUN_1804E1380.
inline int& preferred_team_index(libmcc::s_game_player_options& p) {
	return reinterpret_cast<int&>(p.unknown_2);
}
inline int  preferred_team_index(const libmcc::s_game_player_options& p) {
	return p.unknown_2;
}

// +0x14 — local player count for this peer (sourced from MCC's
// Network_GetMachinePlayerCount → machine+0x62). Init 0. No engine readers
// found — MCC consumes it pre-stamp. (was unknown_3, low byte only.)
inline uint8_t& local_player_count(libmcc::s_game_player_options& p) {
	return *reinterpret_cast<uint8_t*>(&p.unknown_3);
}
inline uint8_t  local_player_count(const libmcc::s_game_player_options& p) {
	return *reinterpret_cast<const uint8_t*>(&p.unknown_3);
}

// +0x18 — peer/machine index. Equality-tested against the local peer ID to
// find local players. Init -1. Verified: haloreach FUN_18040BA68,
// halo3 FUN_180046DF4, halo2 FUN_18056C890.
inline int& peer_machine_index(libmcc::s_game_player_options& p) {
	return reinterpret_cast<int&>(p.unknown_4);
}
inline int  peer_machine_index(const libmcc::s_game_player_options& p) {
	return p.unknown_4;
}

// +0x1C — local controller index (gamepad slot). Paired with
// peer_machine_index under the same xuid match in haloreach FUN_180450A4C →
// fed to spawn allocator FUN_180453218. Init -1.
inline int& local_controller_index(libmcc::s_game_player_options& p) {
	return reinterpret_cast<int&>(p.unknown_5);
}
inline int  local_controller_index(const libmcc::s_game_player_options& p) {
	return p.unknown_5;
}

// -----------------------------------------------------------------------------
// Compile-time offset checks — guards against libmcc layout drift.
// If libmcc's struct is ever bumped, these will fire at compile time.
// -----------------------------------------------------------------------------
static_assert(offsetof(libmcc::s_game_options, un_2)         == 0x50,
	"un_2 (host_secure_address) expected at +0x50");
static_assert(offsetof(libmcc::s_game_options, host_address) == 0x58,
	"host_address expected at +0x58");
static_assert(offsetof(libmcc::s_game_options, un_3)         == 0x2F8,
	"un_3 (local_network_id) expected at +0x2F8");
static_assert(offsetof(libmcc::s_game_options, un_4)         == 0x1CF00,
	"un_4 (game_variant_custom_data) expected at +0x1CF00");
static_assert(offsetof(libmcc::s_game_options, un_5)         == 0x2BB08,
	"un_5 (map_variant_custom_data) expected at +0x2BB08");
static_assert(offsetof(libmcc::s_game_options, un_6)         == 0x2BF10,
	"un_6 (saved_game_state_size) expected at +0x2BF10");
static_assert(offsetof(libmcc::s_game_options, un_7)         == 0x2BF28,
	"un_7 (saved_film_root_path) expected at +0x2BF28");
static_assert(sizeof(libmcc::s_game_options) == 0x2BF30,
	"libmcc::s_game_options size drift");

}  // namespace halox
