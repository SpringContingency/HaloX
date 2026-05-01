#pragma once

// MCC networking bridge — routing seam between halox-loaded game DLLs
// (haloreach/halo3/etc.) and MCC's native networking stack.
//
// Architecture (RE'd from MCC-Win64-Shipping.exe via Ghidra):
//
//   game DLL  --(send_packet vptr)-->  MCC-Win64-Shipping.exe
//                                        +0x1EA298  NetworkStats_RecordOutgoingMessage
//                                        +0x1EA334  NetworkStats_RecordMessageReceive trampoline
//                                        +0x473744  NetworkSession_GetCurrent
//                                        +0x474244  NetworkStats_RecordMessageSend (real)
//                                        +0x4743D0  NetworkStats_RecordMessageReceive (real)
//                                       (driver vtable hangs off *(session+8) →
//                                        impl in halonetworklayer_ship.dll)
//
//   simplenetworklibrary-x64-release.dll  // raw transport, sits below
//   halonetworklayer_ship.dll              // session/driver layer
//
// haloreach.dll exports only 3 symbols and imports zero MCC functions; its
// `send_packet` slot is plugged in at runtime. The bridge here exposes thin
// trampolines so a future hook can install its function pointer pointing
// into mcc_bridge_send / mcc_bridge_recv.
//
// CURRENT LIMITATIONS (research scaffold, not finished feature):
//
//   - MCC-Win64-Shipping.exe is loaded as a DATAFILE (no DllMain run, no
//     relocations applied to the image, no IAT fixups). The PE is mapped as
//     a sequence of file bytes with the section table laid out as on disk,
//     not in memory. We resolve RVAs against the in-memory mapping, which
//     means absolute calls / direct rip-relative addressing inside MCC's
//     functions reference UNRELOCATED file offsets. **CALLING THESE
//     POINTERS WILL CRASH.** This module's resolved pointers are useful
//     only for inspection / future bring-up where we map MCC properly.
//
//   - Even if MCC were mapped as a true image, NetworkSession_GetCurrent
//     reads a singleton that's only populated when MCC's full bootstrap
//     ran. Halox doesn't run that bootstrap, so the singleton is NULL and
//     the trampolines short-circuit (recv returns 0 early; send dereferences
//     NULL inside RecordMessageSend → crash).
//
//   - The actual long-term fix is one of:
//       (a) instantiate MCC's NetworkSession from inside halox by replicating
//           its construction sequence, or
//       (b) bypass MCC entirely and drive halonetworklayer_ship.dll directly
//           (228 NONAME ordinals — needs separate ordinal-table RE).
//
//   The init log line below firing on halox startup is the only deliverable.
//   Wiring into game-DLL send_packet vtable slots is gated on the above.

#include <cstdint>

namespace halox::network {

// Loads halonetworklayer_ship.dll + simplenetworklibrary-x64-release.dll
// as normal DLLs, opens MCC-Win64-Shipping.exe as a datafile mapping, and
// resolves the four key MCC RVAs. Idempotent — safe to call multiple times.
// Logs every step + every individual resolution failure.
void mcc_bridge_init();

// True only after a fully-successful init: all three modules resolved AND
// all four MCC pointers non-null. False on any partial failure.
bool mcc_bridge_ready();

// Thin wrapper around MCC's NetworkStats_RecordOutgoingMessage. Calls
// through MCC's send trampoline at +0x1EA298. SEH-guarded; logs and
// returns silently on AV (expected until the limitations above are
// resolved).
void mcc_bridge_send(uint32_t peer_id,
                     const void* buf,
                     uint32_t len,
                     uint32_t type);

// Thin wrapper around MCC's NetworkStats_RecordMessageReceive trampoline
// at +0x1EA334. Returns:
//   >0  bytes received (out_peer populated)
//    0  idle / no pending message
//   -1  no NetworkSession (singleton NULL) — graceful early-out
//   -2  bridge not initialized
//   -3  SEH faulted inside MCC (offsets drifted or singleton invalid)
int mcc_bridge_recv(void* buf, uint32_t len, uint32_t* out_peer);

} // namespace halox::network
