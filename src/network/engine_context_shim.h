#pragma once

// engine_context_shim — routes haloreach.dll's networking through halox's
// own UDP transport instead of MCC's networking stack.
//
// Reach's `SessionSocket_TrySendPacket` (haloreach RVA 0x43AB88) and the
// recv leaf `FUN_18043aa78` (RVA 0x43AA78) both indirect-call through the
// global `g_engineContext`. The relevant byte offsets on its vtable:
//
//   +0x148   send  signature: void(this, peer_id, buf, len)
//   +0x158   recv  signature: int (this, buf, max_len, *peer_id_out)
//                  -> returns bytes received (0 = idle)
//
// haloreach stores `g_engineContext = param_2` of `Session_LoadJoinDescriptor`
// (= halox's `&g_game_manager` libmcc instance). libmcc's vtable does not
// have valid entries at byte offsets 0x148/0x158 (i_game_manager has ~10
// virtual methods); but reach only ever reads those slots when peer type ==
// 8 (networked), which never happens in halox's solo flow today, so the
// out-of-bounds read is benign in practice.
//
// To make two halox instances on LAN exchange packets through Reach's normal
// send/recv code paths, we:
//   1. Allocate a heap-resident "shim vtable" prefilled with valid pointers.
//   2. Patch byte offsets 0x148 and 0x158 to point at our send/recv.
//   3. Atomically swap the vtable pointer in halox's `g_game_manager` object
//      so reach's `(*g_engineContext)[+0x148/+0x158]` lands in our shim.
//
// The vtable swap is invisible to halox/libmcc itself because the slots
// libmcc uses (the i_game_manager virtual methods) are preserved verbatim
// from the original vtable.
//
// Layer 1 deliverable (this file): the seam itself + UDP transport. The
// shim only fires when reach is actively in MP state (states 6/9/10) and
// has a populated peer table — neither happens in halox's current launch
// path. Layer 2 (separate work) will drive reach into MP and inject a
// peer entry so the slots actually get exercised.

#include <cstdint>

namespace halox::network {

// Initialize the shim:
//   bind_port  — UDP port to bind locally (0 = ephemeral; recommended 27015 / 27016).
//   peer_addr  — IPv4 dotted string for the remote halox (NULL = no remote yet).
//   peer_port  — UDP port the remote is bound to.
//
// On success:
//   * Opens a non-blocking UDP socket bound to (0.0.0.0, bind_port).
//   * Stamps the remote sockaddr_in for outgoing sends.
//   * Allocates a shim vtable copy of `&g_game_manager`'s vtable (RW heap).
//   * Patches byte offsets 0x148 (send) and 0x158 (recv) in the copy.
//   * Atomically swaps the vtable pointer of the `c_game_manager` instance.
//
// Idempotent — first call wins; subsequent calls are no-ops.
// Logs every step (WSAStartup, bind, getaddrinfo, vtable swap).
void engine_context_shim_init(int bind_port,
                              const char* peer_addr,
                              int peer_port);

// Returns true once the shim is fully wired (socket bound + vtable swapped).
bool engine_context_shim_ready();

// Diagnostic counters (lifetime totals since init).
struct s_shim_stats {
    uint64_t sends;            // calls to our slot-0x148 detour
    uint64_t bytes_sent;
    uint64_t recvs_polled;     // calls to our slot-0x158 detour
    uint64_t recvs_returned;   // polls that returned >0 bytes
    uint64_t bytes_received;
    uint64_t send_errors;      // sendto -> SOCKET_ERROR
    uint64_t recv_errors;      // recvfrom -> SOCKET_ERROR (excluding WSAEWOULDBLOCK)
};
void engine_context_shim_get_stats(s_shim_stats* out);

} // namespace halox::network
