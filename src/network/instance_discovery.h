#pragma once

#include <cstdint>
#include <string>
#include <vector>

// instance_discovery — file-based heartbeat service that lets multiple halox
// processes find each other on the same machine. Each instance writes a
// heartbeat file to %TEMP%\halox-instances\<uuid>.halox every 2 seconds with
// its current bind port + active session details. Other instances scan the
// same directory and surface the list to the Session Browser UI.
//
// LAN-across-machines support (UDP broadcast) is a follow-up; the file path
// is sufficient for two halox processes on the same dev box.

namespace halox::network {

struct s_instance_info {
    std::string uuid;
    uint32_t    pid = 0;
    int         bind_port = 0;
    std::string peer_addr;
    int         peer_port = 0;
    std::string hostname;
    std::string label;            // HALOX_INSTANCE env var (e.g. "A", "B")
    uint64_t    last_seen_ms = 0;
    bool        in_game = false;
    int         module = -1;      // libmcc::e_module value (cast at use site)
    int         mode = -1;        // libmcc::e_game_mode
    int         map_id = -1;      // libmcc::e_map_id (or -1 if none)
    int         hopper_game_variant = -1;
    int         hopper_map_variant = -1;
    int         difficulty = -1;
    std::string map_name;
    // Per-process random 64-bit identity. Stamped into Reach's join descriptor
    // at offsets +0x58, +0x60, +0xF8 by the joiner so both peers agree on a
    // session-identity token. Source-of-truth: the host's heartbeat. Captured
    // from a real MCC MP join — see project_haloreach_join_descriptor_capture.
    uint64_t    session_identity = 0;
};

// Initialize. Spawns a background heartbeat thread that writes our file every
// 2 seconds and re-scans the directory for other instances. Idempotent.
void instance_discovery_init();

// Update the "what we're hosting" portion of the heartbeat. Call from the
// Launch path once we know module/mode/map. Pass map_id < 0 to clear.
void instance_discovery_set_session(
    int module, int mode, int map_id,
    int hopper_game_variant, int hopper_map_variant, int difficulty,
    const char* map_name);

// Snapshot of currently-discovered instances (excluding self). Stale entries
// (>6s since last heartbeat) are filtered out.
std::vector<s_instance_info> instance_discovery_list();

// Wake the background thread for an immediate re-scan. The thread already
// scans every 2s; this is for the UI's Refresh button.
void instance_discovery_refresh();

// Read-only metadata about this instance.
const char* instance_discovery_self_uuid();
const char* instance_discovery_self_label();
int         instance_discovery_self_bind_port();

} // namespace halox::network
