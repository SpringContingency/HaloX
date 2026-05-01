#pragma once

// mp_session_inject — drives haloreach into MP state (state 6) and injects
// a peer pointer into the recv-path 2-slot table so the engine_context_shim's
// vtable+0x148/+0x158 hooks actually get exercised.
//
// Layer 2 of the halox networking stack. Layer 1 = engine_context_shim
// (vtable patch + UDP transport). Layer 1 alone is a no-op because reach's
// solo flow never calls slots 0x148/0x158. This file forces:
//
//   1. game_options.game_mode = 3 (MP) — DAT_1824971AC
//   2. campaign gate cleared       — DAT_1824C30C0
//   3. session subsystem-init flag — DAT_182A1FD40 = 1 (usually already set)
//   4. GetNetworkSessionState gate — DAT_182A57F00 = 1, DAT_182A1FD48 = 2
//   5. Session_IsInGameState detoured to return 1 unconditionally (defeats
//      Game_UpdateSessionAndNetworkState's overwrite of +0x60EF8)
//   6. A peer struct (0x20 bytes) allocated with shape:
//        +0x00 u64  unique GUID
//        +0x0C int  -1 (= "use vtable+0x158 path")
//        +0x16 u16  8  (type tag, matches SessionSocket_TrySendPacket
//                        peer.type==8 vtable+0x148 branch)
//      Pointer stamped into *DAT_184E38D30 + 0x10 (slot 0).
//
// All RVAs and globals are documented in
// memory/reference_haloreach_mp_session_layout.md.
//
// Idempotent. Calls AFTER haloreach.dll is loaded; safe at any point after
// initialize_game returns. Run BEFORE the engine thread reaches state 1
// (otherwise the state machine may already have written terminal state 11).

#include <cstdint>

namespace halox::network {

// Configure + apply the injection. Call once per haloreach launch, after
// initialize_game returns and the engine_context_shim is ready.
//
//   peer_guid — any unique non-zero u64. Use a process-id-based value or a
//               hash of HALOX_BIND_PORT / HALOX_PEER_ADDR to keep host vs
//               client distinguishable.
void mp_session_inject_apply(uint64_t peer_guid);

// Stamps the globals every tick to defeat libmcc/MCC overwrites (the Y in
// "deal-breaker — they're written externally"). Spawns a low-priority
// pinning thread that re-applies steps (1)-(2) every 50ms. Idempotent.
void mp_session_inject_start_pinning_thread();

// Diagnostics.
struct s_mp_inject_stats {
    uint32_t is_in_game_state_calls;
    uint32_t globals_pinned_writes;
    uint64_t peer_pointer_at_slot0;
};
void mp_session_inject_get_stats(s_mp_inject_stats* out);

// REAL session init — calls Reach's actual session-bootstrap functions so the
// engine ends up with a properly populated session ctx instead of our 2 MB
// zero buffer. No no-op hooks, no stamp blob. May surface real crashes; that
// is intentional — each crash points at a missing init step we then implement.
//
// Gated on env var HALOX_MP_INJECT_REAL=1. Mutually exclusive with the stub
// path (HALOX_MP_INJECT=1). Pass is_host=true for the side that called
// "Launch", false for the side that clicked "Join".
void mp_session_inject_real_apply(uint64_t peer_guid, bool is_host);

} // namespace halox::network
