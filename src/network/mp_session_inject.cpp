#include "mp_session_inject.h"

// Winsock2.h must precede ANY include that pulls in Windows.h or we get v1
// winsock redefinition conflicts (same hazard as engine_context_shim.cpp).
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "../game/game_manager.h"
#include "../logging/logging.h"

#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace halox::network {

// All RVAs are relative to haloreach.dll's preferred base (0x180000000).
// Confirmed by sub-agent RE pass; documented in memory note
// `reference_haloreach_mp_session_layout`.

static constexpr uintptr_t k_rva_global_subsystem_inited = 0x2A1FD40; // u32, must be 1
static constexpr uintptr_t k_rva_global_state_int       = 0x2A1FD48; // u32, must be ∈{1,2}
static constexpr uintptr_t k_rva_global_state_gate      = 0x2A57F00; // u8,  must be 1
static constexpr uintptr_t k_rva_game_mode              = 0x24971AC; // u32, 3 = MP
static constexpr uintptr_t k_rva_campaign_gate          = 0x24C30C0; // u32, 0
static constexpr uintptr_t k_rva_session_struct         = 0x2B05C80; // base of recv-path session struct
static constexpr uintptr_t k_rva_active_session_ptr     = 0x4E38D30; // u64, points to session struct (above)
static constexpr ptrdiff_t k_session_peer_slot0_off     = 0x10;

// FUN_18038FAE8 dereferences `DAT_182a1fe08`. If NULL, we AV at +0x11 of that
// function (`*(int*)(NULL + 0x60ef8)`). InitializeNetworkSessionSubsystems
// normally writes `DAT_182a1fe08 = DAT_184e38d08` — but that source slot is
// also NULL in halox's launch path (it's populated by an earlier MCC init
// step we don't run). So we VirtualAlloc our own zero-filled struct big
// enough to cover offset 0x60EF8 + state int + slack, then stamp the global
// to point at it. After that, set `*(int*)(struct + 0x60ef8) = 4` so
// FUN_18038FAE8 returns true (range [4,10] = "valid in-game state").
static constexpr uintptr_t k_rva_session_ctx_ptr_global = 0x2A1FE08; // u64, target pointer
static constexpr uintptr_t k_rva_session_ctx_d08        = 0x4E38D08; // u64, sibling — UpdateMapTransitionFrame uses this as 4-instance base
static constexpr ptrdiff_t k_session_ctx_state_off      = 0x60EF8;   // in-game state int
static constexpr ptrdiff_t k_session_ctx_instance_stride = 0x61668;  // UpdateMapTransitionFrame iterates 4 slots stride 0x61668
static constexpr size_t    k_session_ctx_size           = 0x200000;  // 2 MB — covers 4 instances (4 * 0x61668 = 0x185A20) + slack
static void*               g_session_ctx_buffer         = nullptr;

static constexpr uintptr_t k_rva_session_is_in_game_state = 0x2A31CC; // hook target
static constexpr uintptr_t k_rva_bandwidth_stats_tick    = 0x3C4794;  // FUN_1803c4794, divides by *(int*)(p1+0x28)
static constexpr uintptr_t k_rva_try_init_map_or_migrate = 0x38EE28;  // Session_TryInitiateMapLoadOrHostMigration — no-op for 2-instance LAN
static constexpr uintptr_t k_rva_network_dispatch_frame  = 0x38E5D8;  // bypass — derefs DAT_182a1fd30[0..3], all NULL in halox path
static constexpr uintptr_t k_rva_network_frame_tick      = 0x38F49C;  // called by dispatch — we call directly with our session ctx
static constexpr uintptr_t k_rva_session_proc_net_timers = 0x40719C;  // Session_ProcessNetworkAndTimers — vtable call on null buffer
static constexpr uintptr_t k_rva_sess_conn_update_state  = 0x393830;  // SessionConnection_UpdateState — needs real connection state
static constexpr uintptr_t k_rva_throughput_metrics_tick = 0x443D7C;  // FUN_180443d7c — derefs NULL DAT_182d67250 stats struct
static constexpr uintptr_t k_rva_get_net_session_state   = 0x2A4088;  // GetNetworkSessionState — return 2 always so we can stamp slot 3 with pointer
static constexpr uintptr_t k_rva_game_update_session     = 0x39A0BC;  // Game_UpdateSessionAndNetworkState — keeps AVing on session ctx pointer even with stamps

using IsInGameStateFn = char(*)();
static IsInGameStateFn   g_orig_is_in_game_state = nullptr;

using BandwidthStatsTickFn = void(*)(uintptr_t);
static BandwidthStatsTickFn g_orig_bandwidth_stats_tick = nullptr;

using TryInitMapOrMigrateFn = void(*)();
static TryInitMapOrMigrateFn g_orig_try_init_map_or_migrate = nullptr;

using NetworkFrameTickFn = void(*)(uintptr_t);
static NetworkFrameTickFn g_network_frame_tick = nullptr;

using NetworkDispatchFrameFn = void(*)();
static NetworkDispatchFrameFn g_orig_network_dispatch_frame = nullptr;

using SessionProcNetTimersFn = void(*)(int*);
static SessionProcNetTimersFn g_orig_session_proc_net_timers = nullptr;

using SessConnUpdateStateFn = void(*)(uintptr_t);
static SessConnUpdateStateFn g_orig_sess_conn_update_state = nullptr;

using ThroughputMetricsTickFn = void(*)(uint64_t, char);
static ThroughputMetricsTickFn g_orig_throughput_metrics_tick = nullptr;

using GetNetSessStateFn = uint64_t(*)();
static GetNetSessStateFn g_orig_get_net_session_state = nullptr;

using GameUpdateSessionFn = void(*)();
static GameUpdateSessionFn g_orig_game_update_session = nullptr;

static std::atomic<bool> g_applied{false};
static std::atomic<bool> g_pinning_started{false};
static volatile uint32_t g_calls_to_is_in_game_state = 0;
static volatile uint32_t g_pinning_writes = 0;
static uint64_t          g_peer_slot0_value = 0;

// Single peer struct, allocated permanent for the session. Layout:
//   +0x00 u64  GUID
//   +0x08 u64  socket handle (unused — vtable path bypasses)
//   +0x0C int  -1            (sentinel: take vtable+0x158 path)
//   +0x10 u16  0
//   +0x16 u16  8             (type tag — matches +0x12 == 8 in send path)
//   ...
// Allocated as zeroed 0x20 buffer.
static uint8_t           g_peer_struct[0x20] = {};

static char hook_session_is_in_game_state() {
    g_calls_to_is_in_game_state++;
    // Return 6: middle of the [4,9] range that Player_CanUseEquipment's
    // early-out check insists on. Was 1 in the legacy stub path which is
    // out-of-range and caused the join-status oracle to bail. 6 also reads as
    // "in game" to any caller using the value as a boolean. See decompile of
    // Player_CanUseEquipment (the misnamed join-status oracle Reach polls
    // from Session_JoinUpdateTick).
    return 6;
}

// FUN_1803c4794 = bandwidth-stats rolling-window tick. It computes
// `iVar3 - iVar5 % *(int*)(param_1 + 0x28)`. When param_1 lives in halox's
// zero-allocated buffer, `+0x28` is 0 → STATUS_INTEGER_DIVIDE_BY_ZERO.
// Bail before the divide when the bucket-size field is unset.
static void hook_bandwidth_stats_tick(uintptr_t param_1) {
    if (param_1 == 0 || *reinterpret_cast<int32_t*>(param_1 + 0x28) == 0) {
        return; // skip the divide; stats not initialized for this object
    }
    g_orig_bandwidth_stats_tick(param_1);
}

// Session_TryInitiateMapLoadOrHostMigration. Reach's host-disconnect handler.
// Dereferences `local_170 + 0x4a20` early — local_170 is whatever the inner
// FUN_18038fb68 call wrote there. In halox's path that pointer is invalid,
// AVing immediately. For 2-instance LAN we don't need host migration to
// work — both ends are stable. No-op the entire function.
static void hook_try_init_map_or_migrate() {
    // intentional no-op
}

// Game_UpdateSessionAndNetworkState reads DAT_182a1fe08 (session ctx slot 0)
// at +0xBE and derefs `+0x60ef8`. Even with our stamp + 5ms pinning thread,
// the engine occasionally races us and sees NULL → AV. This function's work
// (peer-state heartbeat, controller arbitration) isn't needed for our LAN
// MP path — UDP traffic goes through engine_context_shim. No-op for now.
static void hook_game_update_session() {
    // intentional no-op
}

// GetNetworkSessionState normally reads `*(int*)0x182a1fd48 ∈ {1,2}` to return
// 2. We stamp slot 3 of the DAT_182a1fd30 4-slot pointer table (= 0x182a1fd48)
// with a pointer (so NetworkDispatchFrame's slot iterator can deref it safely).
// That overwrites the int with the low 32 bits of an arbitrary heap address,
// breaking the {1,2} check. Hook to unconditionally return 2.
static uint64_t hook_get_net_session_state() {
    return 2;
}

// FUN_180443d7c — throughput/voice metrics tick called once per peer-state
// pass. Reads ~10 fields from a NULL stats struct (DAT_182d67250) at the top
// of the function. No-op for LAN MP — we don't need throughput tracking.
static void hook_throughput_metrics_tick(uint64_t /*p1*/, char /*p2*/) {
    // intentional no-op
}

// SessionConnection_UpdateState dispatches on `*(int*)(p1 + 0x60ef8)` (the
// per-connection state) and fans out to per-state handlers (heartbeat tick,
// connection control, peer kicks, validation). Each handler walks real
// connection state we never built. Called only from UpdateMapTransitionFrame
// in a 4-instance loop; no-op here is safe — the actual UDP send/recv path
// goes through engine_context_shim's vtable on g_game_manager, not this loop.
static void hook_sess_conn_update_state(uintptr_t /*param_1*/) {
    // intentional no-op
}

// Session_ProcessNetworkAndTimers iterates pending channel events, then makes
// a vtable call `(**(code**)(*plVar1 + 8))(plVar1)` where plVar1 is a channel
// slot from DAT_182a1fd50+. We point those slots at our zero buffer so the
// flag-byte reads in ProcessPendingChannelEvents skip, but the vtable deref
// still crashes (`*(qword*)0 + 8`). Skipping the entire function is safe:
// actual UDP send/recv routes through engine_context_shim's vtable on
// g_game_manager, not through this controller/heartbeat tick.
static void hook_session_proc_net_timers(int* /*param_1*/) {
    // intentional no-op — see above
}

// NetworkDispatchFrame iterates 4 session-ctx slots at DAT_182a1fd30 and
// derefs `slot_ptr + 0x60ef8`. In halox's path slot 0 is NULL → AV at +0x100
// of the function. Slots 1..3 are also NULL, and slot 3 (offset 0x18) shares
// memory with DAT_182a1fd48 = 2 (the network-state int we stamped) so we
// can't simply pre-populate the slot table without breaking GetNetworkSessionState.
// Bypass: skip the peer-cleanup loop, call NetworkFrameTick directly with our
// own session ctx (which has +0x60ef8 = 4, in the [4,9] valid range).
static void hook_network_dispatch_frame() {
    if (g_session_ctx_buffer && g_network_frame_tick) {
        g_network_frame_tick(reinterpret_cast<uintptr_t>(g_session_ctx_buffer));
    }
}

static HMODULE haloreach_module() {
    return GetModuleHandleW(L"haloreach.dll");
}

static bool stamp_globals(uintptr_t base) {
    // Writable .data globals. DAT_182a1fd40 (subsystem-inited flag) is set
    // LAST — Game_UpdateSessionAndNetworkState gates its session-ctx pointer
    // reads on this flag, and setting it before stamping the pointers opens
    // a race where the engine thread reads NULL. DAT_182a1fd48 (state int)
    // IS stamped to 2 — many callers read it as a small int (array index,
    // state machine value), and our hooks (NetworkDispatchFrame bypass etc.)
    // already make slot 3 of the DAT_182a1fd30 table irrelevant.
    *reinterpret_cast<uint32_t*>(base + k_rva_global_state_int)       = 2;
    *reinterpret_cast<uint8_t*> (base + k_rva_global_state_gate)      = 1;
    *reinterpret_cast<uint32_t*>(base + k_rva_game_mode)              = 3;
    *reinterpret_cast<uint32_t*>(base + k_rva_campaign_gate)          = 0;

    // Make sure FUN_18038FAE8 + Game_UpdateSessionAndNetworkState don't AV.
    // The 4-slot session-ctx pointer table at DAT_182a1fe08..fe20 is read by
    // Game_UpdateSessionAndNetworkState early via piVar22..piVar24 and each
    // slot is dereffed at `+0x60ef8`. In halox's path all 4 slots are NULL.
    // VirtualAlloc one zero-filled buffer and point ALL FOUR slots at it,
    // then stamp the in-game state field at +0x60EF8 to 4 (in [4,9] = valid).
    if (!g_session_ctx_buffer) {
        g_session_ctx_buffer = VirtualAlloc(nullptr, k_session_ctx_size,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_session_ctx_buffer) {
            CONSOLE_LOG_WARN("mp_inject: VirtualAlloc(%zu) for session ctx failed (gle=%lu)",
                k_session_ctx_size, GetLastError());
            return false;
        }
        CONSOLE_LOG_INFO("mp_inject: allocated session-ctx buffer %p (%zu zero bytes)",
            g_session_ctx_buffer, k_session_ctx_size);
    }
    auto buf_addr = reinterpret_cast<uintptr_t>(g_session_ctx_buffer);
    auto* ctx_table = reinterpret_cast<uintptr_t*>(base + k_rva_session_ctx_ptr_global);
    for (int i = 0; i < 4; ++i) {
        if (ctx_table[i] == 0) ctx_table[i] = buf_addr;
    }
    CONSOLE_LOG_INFO("mp_inject: session-ctx slots[0..3] @ %p..%p stamped to %p",
        (void*)ctx_table, (void*)(ctx_table + 3), (void*)buf_addr);
    *reinterpret_cast<uint32_t*>(buf_addr + k_session_ctx_state_off) = 4;

    // DAT_184E38D20 is the variant-stream context pointer passed to
    // SessionConnection_FlushAndDispatchVariantStream (UpdateMapTransitionFrame
    // calls it). NULL → AV reading +0x28. Stamping our zero buffer makes
    // `*(char*)(p1+0x28) != 0` false → entire function body short-circuits.
    // Normally written by Session_SetupGlobalsAndSimulation which we don't run.
    auto* d20_slot = reinterpret_cast<uintptr_t*>(base + 0x4E38D20);
    if (*d20_slot == 0) {
        *d20_slot = buf_addr;
        CONSOLE_LOG_INFO("mp_inject: stamped DAT_184e38d20 -> %p (variant-stream ctx)", (void*)buf_addr);
    }

    // DAT_184E38D08 is the sibling base used by UpdateMapTransitionFrame, which
    // iterates 4 sub-instances at stride 0x61668 and derefs each at +0x60EF8.
    // Per the memory note, DAT_184E38D08 normally equals DAT_182A1FE08. Point
    // it at the same buffer; sub-instance 0 reuses our state=4, instances 1..3
    // see state=0 (zero-initialized) and SessionConnection_UpdateState early-exits.
    auto* d08_slot = reinterpret_cast<uintptr_t*>(base + k_rva_session_ctx_d08);
    if (*d08_slot == 0) {
        *d08_slot = buf_addr;
        CONSOLE_LOG_INFO("mp_inject: stamped DAT_184e38d08 -> %p (4 instances stride 0x%X)",
            (void*)buf_addr, (unsigned)k_session_ctx_instance_stride);
    }

    // DAT_182a1fd30 is a 4-slot session-ctx pointer table read by
    // NetworkDispatchFrame's loop. We hook that function to bypass the loop,
    // but stamp slots 0..2 anyway in case any caller iterates them before
    // our hook is live. Slot 3 (offset 0x18) overlaps DAT_182a1fd48 (state
    // int) which must remain 2 for ProcessPendingChannelEvents and other
    // callers that index off it — leave it alone.
    auto* fd30_table = reinterpret_cast<uintptr_t*>(base + 0x2A1FD30);
    for (int i = 0; i < 3; ++i) {
        fd30_table[i] = buf_addr;
    }
    CONSOLE_LOG_INFO("mp_inject: pre-stamped DAT_182a1fd30 slots[0..2] -> %p", (void*)buf_addr);

    // Channel-pointer array at DAT_182a1fd50..DAT_182a1fe00 (between the state
    // int at fd48 and the session-ctx slots starting at fe08). Engine passes
    // `&DAT_182a1fd48` to Session_ProcessNetworkAndTimers; nested
    // ProcessPendingChannelEvents indexes channels via `(state*2+2) ints`
    // — with state=2 → +0x18 → DAT_182a1fd60, and reads `+0x38` byte for
    // flag bits. NULL slot → AV. Point every qword in the channel-array
    // region at our zero buffer so flag-byte reads return 0 and all
    // SendEventPacket branches are skipped.
    auto* channel_array = reinterpret_cast<uintptr_t*>(base + 0x2A1FD50);
    constexpr size_t channel_array_qwords = (0x2A1FE00 - 0x2A1FD50) / 8;
    for (size_t i = 0; i < channel_array_qwords; ++i) {
        if (channel_array[i] == 0) channel_array[i] = buf_addr;
    }
    CONSOLE_LOG_INFO("mp_inject: channel-array slots @ %p..%p (%zu qwords) stamped to %p",
        (void*)channel_array, (void*)(channel_array + channel_array_qwords - 1),
        channel_array_qwords, (void*)buf_addr);

    // SessionManager_PollIncomingPackets reads `DAT_184e38d30` and dereferences
    // `lVar2 + 0x10` — also NULL in halox's path. Point it at the static
    // session struct at RVA 0x2B05C80 (where Session_SetupGlobalsAndSimulation
    // would have written it) so the recv loop can iterate the peer table.
    auto* active_session_slot = reinterpret_cast<uintptr_t*>(base + k_rva_active_session_ptr);
    if (*active_session_slot == 0) {
        *active_session_slot = base + k_rva_session_struct;
        CONSOLE_LOG_INFO("mp_inject: stamped DAT_184e38d30 -> haloreach+0x%X (was NULL)",
            (unsigned)k_rva_session_struct);
    }

    // Last: flip the subsystem-inited flag. Game_UpdateSessionAndNetworkState
    // checks this before reading session-ctx pointers we just stamped. Setting
    // it earlier opens a window where the engine thread reads NULL pointers.
    *reinterpret_cast<uint32_t*>(base + k_rva_global_subsystem_inited) = 1;
    CONSOLE_LOG_INFO("mp_inject: subsystem-inited flag set (all pointers safe to read)");
    return true;
}

static bool inject_peer(uintptr_t base, uint64_t peer_guid) {
    // Build the peer struct in-place.
    memset(g_peer_struct, 0, sizeof(g_peer_struct));
    *reinterpret_cast<uint64_t*>(g_peer_struct + 0x00) = peer_guid;
    *reinterpret_cast<int32_t*> (g_peer_struct + 0x0C) = -1; // vtable path
    *reinterpret_cast<uint16_t*>(g_peer_struct + 0x16) = 8;  // type tag = 8

    // Stamp the peer pointer into session+0x10 (slot 0).
    auto* slot0 = reinterpret_cast<uint64_t*>(base + k_rva_session_struct + k_session_peer_slot0_off);
    g_peer_slot0_value = reinterpret_cast<uint64_t>(g_peer_struct);
    *slot0 = g_peer_slot0_value;

    CONSOLE_LOG_INFO("mp_inject: peer struct @ %p stamped into session+0x10 @ %p (guid=0x%016llX)",
        (void*)g_peer_struct, (void*)slot0, (unsigned long long)peer_guid);
    return true;
}

static bool install_is_in_game_state_hook(uintptr_t base) {
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        CONSOLE_LOG_WARN("mp_inject: MH_Initialize failed");
        return false;
    }
    void* target = reinterpret_cast<void*>(base + k_rva_session_is_in_game_state);
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&hook_session_is_in_game_state),
                                reinterpret_cast<void**>(&g_orig_is_in_game_state));
    if (s != MH_OK) {
        CONSOLE_LOG_WARN("mp_inject: MH_CreateHook(IsInGameState @ %p) failed (%d)", target, (int)s);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        CONSOLE_LOG_WARN("mp_inject: MH_EnableHook(IsInGameState) failed");
        return false;
    }
    CONSOLE_LOG_INFO("mp_inject: Session_IsInGameState detour installed @ %p (always returns 1)", target);

    // Bandwidth-stats divide-by-zero guard.
    void* bw_target = reinterpret_cast<void*>(base + k_rva_bandwidth_stats_tick);
    if (MH_CreateHook(bw_target,
                      reinterpret_cast<void*>(&hook_bandwidth_stats_tick),
                      reinterpret_cast<void**>(&g_orig_bandwidth_stats_tick)) == MH_OK) {
        MH_EnableHook(bw_target);
        CONSOLE_LOG_INFO("mp_inject: bandwidth_stats_tick guard installed @ %p", bw_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: bandwidth_stats_tick guard MH_CreateHook failed");
    }

    // Host-migration handler — no-op for 2-instance LAN.
    void* mig_target = reinterpret_cast<void*>(base + k_rva_try_init_map_or_migrate);
    if (MH_CreateHook(mig_target,
                      reinterpret_cast<void*>(&hook_try_init_map_or_migrate),
                      reinterpret_cast<void**>(&g_orig_try_init_map_or_migrate)) == MH_OK) {
        MH_EnableHook(mig_target);
        CONSOLE_LOG_INFO("mp_inject: try_init_map_or_migrate no-op installed @ %p", mig_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: try_init_map_or_migrate hook MH_CreateHook failed");
    }

    // NetworkDispatchFrame bypass — we route to NetworkFrameTick(our_session_ctx)
    // directly to avoid the slot-table deref.
    g_network_frame_tick = reinterpret_cast<NetworkFrameTickFn>(base + k_rva_network_frame_tick);
    void* dispatch_target = reinterpret_cast<void*>(base + k_rva_network_dispatch_frame);
    if (MH_CreateHook(dispatch_target,
                      reinterpret_cast<void*>(&hook_network_dispatch_frame),
                      reinterpret_cast<void**>(&g_orig_network_dispatch_frame)) == MH_OK) {
        MH_EnableHook(dispatch_target);
        CONSOLE_LOG_INFO("mp_inject: NetworkDispatchFrame bypass installed @ %p (routes to NetworkFrameTick @ %p)",
            dispatch_target, (void*)g_network_frame_tick);
    } else {
        CONSOLE_LOG_WARN("mp_inject: NetworkDispatchFrame hook MH_CreateHook failed");
    }

    // Session_ProcessNetworkAndTimers — no-op (vtable call on null channel buffer).
    void* spt_target = reinterpret_cast<void*>(base + k_rva_session_proc_net_timers);
    if (MH_CreateHook(spt_target,
                      reinterpret_cast<void*>(&hook_session_proc_net_timers),
                      reinterpret_cast<void**>(&g_orig_session_proc_net_timers)) == MH_OK) {
        MH_EnableHook(spt_target);
        CONSOLE_LOG_INFO("mp_inject: Session_ProcessNetworkAndTimers no-op installed @ %p", spt_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: Session_ProcessNetworkAndTimers hook MH_CreateHook failed");
    }

    // SessionConnection_UpdateState — no-op (per-connection state machine over
    // null buffer; handlers walk fields we never populated).
    void* scu_target = reinterpret_cast<void*>(base + k_rva_sess_conn_update_state);
    if (MH_CreateHook(scu_target,
                      reinterpret_cast<void*>(&hook_sess_conn_update_state),
                      reinterpret_cast<void**>(&g_orig_sess_conn_update_state)) == MH_OK) {
        MH_EnableHook(scu_target);
        CONSOLE_LOG_INFO("mp_inject: SessionConnection_UpdateState no-op installed @ %p", scu_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: SessionConnection_UpdateState hook MH_CreateHook failed");
    }

    // Game_UpdateSessionAndNetworkState — no-op (NULL session-ctx ptr races even
    // with 5ms pinning).
    void* gus_target = reinterpret_cast<void*>(base + k_rva_game_update_session);
    if (MH_CreateHook(gus_target,
                      reinterpret_cast<void*>(&hook_game_update_session),
                      reinterpret_cast<void**>(&g_orig_game_update_session)) == MH_OK) {
        MH_EnableHook(gus_target);
        CONSOLE_LOG_INFO("mp_inject: Game_UpdateSessionAndNetworkState no-op installed @ %p", gus_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: Game_UpdateSessionAndNetworkState hook MH_CreateHook failed");
    }

    // GetNetworkSessionState — return 2 always (we no longer maintain the int
    // at DAT_182a1fd48 because that memory is overlaid by slot 3 of the
    // DAT_182a1fd30 pointer table).
    void* gns_target = reinterpret_cast<void*>(base + k_rva_get_net_session_state);
    if (MH_CreateHook(gns_target,
                      reinterpret_cast<void*>(&hook_get_net_session_state),
                      reinterpret_cast<void**>(&g_orig_get_net_session_state)) == MH_OK) {
        MH_EnableHook(gns_target);
        CONSOLE_LOG_INFO("mp_inject: GetNetworkSessionState detour installed @ %p (always returns 2)", gns_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: GetNetworkSessionState hook MH_CreateHook failed");
    }

    // FUN_180443d7c throughput-metrics tick — no-op (NULL stats struct).
    void* tm_target = reinterpret_cast<void*>(base + k_rva_throughput_metrics_tick);
    if (MH_CreateHook(tm_target,
                      reinterpret_cast<void*>(&hook_throughput_metrics_tick),
                      reinterpret_cast<void**>(&g_orig_throughput_metrics_tick)) == MH_OK) {
        MH_EnableHook(tm_target);
        CONSOLE_LOG_INFO("mp_inject: throughput_metrics_tick no-op installed @ %p", tm_target);
    } else {
        CONSOLE_LOG_WARN("mp_inject: throughput_metrics_tick hook MH_CreateHook failed");
    }
    return true;
}

// ---- Real-path minimal guards --------------------------------------------
//
// The real-path bootstrap (Session_LoadJoinDescriptor + the engine init
// thread it spawns) handles the hard work, but Reach's join state machine
// (state 9) polls Player_CanUseEquipment(0) which insists on
// `*(int*)(DAT_182a1fe08 + 0x60ef8)` being in [4,9]. The earlier attempt
// to satisfy this with a 5ms pinner thread that wrote 6 into the field
// CORRUPTED the in-progress session's handle table (other threads
// crashed in FUN_18001398c+0x26 / FUN_18001251c+0x16 right after).
//
// Replacement strategy: detour Player_CanUseEquipment(0) to return 0
// (success) directly. Single-purpose, no engine-memory writes, no race
// with subsystem threads. Other call sites (param_1 != 0) defer to the
// original implementation.
//
// Plus: hook Session_IsInGameState to return 6 for the function-form
// callers (harmless when correct; required for some legacy code paths).

static std::atomic<bool> g_real_guards_armed{false};

static constexpr uintptr_t k_rva_player_can_use_equipment = 0x39B558;
using PlayerCanUseEquipmentFn = int(*)(int);
static PlayerCanUseEquipmentFn g_orig_player_can_use_equipment = nullptr;

static volatile uint32_t g_pce_calls_zero = 0;
static volatile uint32_t g_pce_calls_other = 0;

static int hook_player_can_use_equipment(int param_1) {
    if (param_1 == 0) {
        g_pce_calls_zero++;
        return 0;
    }
    g_pce_calls_other++;
    if (g_orig_player_can_use_equipment) {
        return g_orig_player_can_use_equipment(param_1);
    }
    return 1;
}

// Watchdog: after the worker exits, periodically dump state machine + hook
// counters so we can see whether Session_JoinUpdateTick ever runs (and thus
// whether our Player_CanUseEquipment hook ever sees param_1==0).
static DWORD WINAPI real_watchdog_proc(LPVOID) {
    HMODULE mod = haloreach_module();
    if (!mod) return 0;
    auto base = reinterpret_cast<uintptr_t>(mod);
    for (int i = 0; i < 20; i++) {
        Sleep(1000);
        uint32_t sm   = *reinterpret_cast<uint32_t*>(base + 0xC1A114);
        uint8_t  ji   = *reinterpret_cast<uint8_t*> (base + 0xC1A0D0);
        uint8_t  ce2  = *reinterpret_cast<uint8_t*> (base + 0x4E38CE2);
        CONSOLE_LOG_INFO("mp_inject_real: watchdog t=%ds sm=%u join_initiated=%u ce2=%u "
                         "pce0=%u pce_other=%u iigs=%u",
            i + 1, (unsigned)sm, (unsigned)ji, (unsigned)ce2,
            (unsigned)g_pce_calls_zero, (unsigned)g_pce_calls_other,
            (unsigned)g_calls_to_is_in_game_state);
        if (sm == 0xB) {
            CONSOLE_LOG_INFO("mp_inject_real: watchdog state machine reached 0xB (in-game) — exiting");
            break;
        }
    }
    return 0;
}

static bool install_real_path_minimal_guards(uintptr_t base) {
    bool expected = false;
    if (!g_real_guards_armed.compare_exchange_strong(expected, true)) return true;

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_Initialize failed (guards)");
        g_real_guards_armed = false;
        return false;
    }

    // Session_IsInGameState — function-form callers expect a [4,9] value.
    void* iigs_target = reinterpret_cast<void*>(base + k_rva_session_is_in_game_state);
    MH_STATUS s = MH_CreateHook(iigs_target,
                                reinterpret_cast<void*>(&hook_session_is_in_game_state),
                                reinterpret_cast<void**>(&g_orig_is_in_game_state));
    if (s == MH_OK) {
        MH_EnableHook(iigs_target);
        CONSOLE_LOG_INFO("mp_inject_real: Session_IsInGameState hook installed @ %p (returns 6)", iigs_target);
    } else if (s != MH_ERROR_ALREADY_CREATED) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_CreateHook(IsInGameState) failed (%d)", (int)s);
    }

    // Player_CanUseEquipment — return 0 for join-status path (param_1==0),
    // else defer to original. Replaces the dangerous 5ms state pinner.
    void* pce_target = reinterpret_cast<void*>(base + k_rva_player_can_use_equipment);
    s = MH_CreateHook(pce_target,
                      reinterpret_cast<void*>(&hook_player_can_use_equipment),
                      reinterpret_cast<void**>(&g_orig_player_can_use_equipment));
    if (s == MH_OK) {
        MH_EnableHook(pce_target);
        CONSOLE_LOG_INFO("mp_inject_real: Player_CanUseEquipment hook installed @ %p (param_1==0 returns 0)", pce_target);
    } else if (s != MH_ERROR_ALREADY_CREATED) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_CreateHook(Player_CanUseEquipment) failed (%d)", (int)s);
    }
    return true;
}

// ---- Real-init path (no zero buffer, no no-op cocktail) -------------------
//
// Calls Reach's actual session-bootstrap functions so the engine ends up with
// a properly populated session ctx. Each step is logged + state-dumped so we
// can see exactly where the engine progresses to (or crashes). Layered with
// the existing stub path via env var so the user can A/B test.

static constexpr uintptr_t k_rva_session_master_init     = 0x3AD858; // FUN_1803ad858 — full session init chain (zero params)
static constexpr uintptr_t k_rva_engine_net_main_loop    = 0x3AE640; // Engine_NetworkMainLoopTick — engine thread tick (memory ready by here)
static constexpr uintptr_t k_rva_load_join_descriptor    = 0xF118;   // Session_LoadJoinDescriptor — MCC's "here is the host to join" entry
static constexpr size_t    k_join_descriptor_size        = 0x2BF30;  // size of the descriptor blob memcpy'd into DAT_1824971A0
static constexpr uintptr_t k_rva_setup_globals_and_sim   = 0x3CFF48; // Session_SetupGlobalsAndSimulation()
static constexpr uintptr_t k_rva_init_net_session_subsys = 0x3993A4; // InitializeNetworkSessionSubsystems(6 params)
static constexpr uintptr_t k_rva_request_session_start   = 0x399FC4; // RequestSessionStart(int state)
static constexpr uintptr_t k_rva_begin_join_request      = 0x39B6D0; // Session_BeginJoinRequest(...)

using SessionMasterInitFn = void(*)();
using SetupGlobalsFn = void(*)();
using InitNetSubsysFn = uint64_t(*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
using RequestSessionStartFn = void(*)(int);
using EngineNetMainLoopFn = void(*)();
using LoadJoinDescriptorFn = void(*)(uint64_t, void*, void*);
static EngineNetMainLoopFn g_orig_engine_net_main_loop = nullptr;
static std::atomic<bool>   g_real_init_fired{false};
static bool                g_real_init_is_host = false;
static uint64_t            g_real_init_peer_guid = 0;

// Permanent descriptor blob fed to Session_LoadJoinDescriptor. Reach memcpy's
// it into DAT_1824971A0 and reads u64 host-address from +0x58. We zero the
// rest — most of the 0x2BF30 bytes are scenario/map/variant payload that
// Reach will populate later via descriptor-update messages from the host.
static uint8_t g_join_descriptor_blob[k_join_descriptor_size] = {};

static std::atomic<bool> g_real_applied{false};

static void log_session_state(uintptr_t base, const char* tag) {
    auto d30  = *reinterpret_cast<uintptr_t*>(base + 0x4E38D30);
    auto d20  = *reinterpret_cast<uintptr_t*>(base + 0x4E38D20);
    auto d08  = *reinterpret_cast<uintptr_t*>(base + 0x4E38D08);
    auto fe08 = *reinterpret_cast<uintptr_t*>(base + 0x2A1FE08);
    auto fd40 = *reinterpret_cast<uint32_t*>(base + 0x2A1FD40);
    auto fd48 = *reinterpret_cast<uint32_t*>(base + 0x2A1FD48);
    auto gate = *reinterpret_cast<uint8_t*>(base + 0x2A57F00);
    auto smstate = *reinterpret_cast<uint32_t*>(base + 0xC1A114);  // ExternalLaunchStateMachine state
    auto smgate  = *reinterpret_cast<uint32_t*>(base + 0x27CD2E4); // gate for the state-machine manager
    auto ec      = *reinterpret_cast<uintptr_t*>(base + 0xC1A100); // g_engineContext
    CONSOLE_LOG_INFO("mp_inject_real: state[%s] sm=%u smgate=%u ec=%p fd40=%u fd48=%u gate=%u "
                     "d30=%p d20=%p d08=%p fe08=%p",
        tag, (unsigned)smstate, (unsigned)smgate, (void*)ec,
        (unsigned)fd40, (unsigned)fd48, (unsigned)gate,
        (void*)d30, (void*)d20, (void*)d08, (void*)fe08);
}

static DWORD WINAPI real_state_dumper_proc(LPVOID) {
    HMODULE mod = haloreach_module();
    if (!mod) return 0;
    auto base = reinterpret_cast<uintptr_t>(mod);
    int i = 0;
    while (true) {
        Sleep(2000);
        char tag[32];
        snprintf(tag, sizeof(tag), "tick-%ds", (i + 1) * 2);
        log_session_state(base, tag);
        i++;
        if (i > 30) break; // 60 seconds total, then stop spamming
    }
    return 0;
}

// Bootstrap worker. Runs OFF the engine net-loop thread so the engine tick
// can return immediately and keep pumping. Reworked 2026-04-29 after capturing
// a real MCC join via ReverseMe — see project_haloreach_join_descriptor_capture
// memory file. Three changes from the prior approach:
//   1. NO manual master_init (FUN_1803ad858) call. That bypasses the
//      EngineStartup → handle-table-CS init that Reach's own
//      Engine_InitializeAndRun does, causing AVs in InitHandleEntry.
//   2. param_2 to Session_LoadJoinDescriptor must be MCC's c_engine_context,
//      not halox's g_game_manager. Read it live from haloreach's own
//      DAT_180c1a100 — by the time the engine tick fires we know MCC has
//      already populated it.
//   3. Descriptor +0x58 is an opaque 8-byte session identity, not (port<<32)|ip.
//      The host advertises its identity via instance_discovery; the joiner
//      reads it through HALOX_PEER_SESSION_ID and stamps it at +0x58/+0x60/
//      +0xF8 to match what the engine state machine expects.
static DWORD WINAPI real_bootstrap_worker(LPVOID) {
    HMODULE mod = haloreach_module();
    if (!mod) return 0;
    auto base = reinterpret_cast<uintptr_t>(mod);
    CONSOLE_LOG_INFO("mp_inject_real: bootstrap worker — host=%d peer_guid=0x%016llX",
        (int)g_real_init_is_host, (unsigned long long)g_real_init_peer_guid);
    log_session_state(base, "worker-pre");

    // Set just the gating globals. Game mode + campaign + state gate. NOTE:
    // we do NOT touch DAT_182a57f00 (the master_init early-exit gate) — we
    // never call master_init.
    *reinterpret_cast<uint32_t*>(base + k_rva_game_mode)     = 3;
    *reinterpret_cast<uint32_t*>(base + k_rva_campaign_gate) = 0;
    *reinterpret_cast<uint8_t*> (base + k_rva_global_state_gate) = 1;

    // Install the small set of real-path guards: IsInGameState hook + state
    // pinner. Needed because the join state machine polls a session_ctx field
    // for [4,9] and the engine never naturally bumps it without a real online
    // handshake. Without this, state machine stalls at 9 indefinitely.
    install_real_path_minimal_guards(base);

    if (g_real_init_is_host) {
        auto request_start = reinterpret_cast<RequestSessionStartFn>(base + k_rva_request_session_start);
        CONSOLE_LOG_INFO("mp_inject_real: calling RequestSessionStart(2) [host]");
        request_start(2);
        CONSOLE_LOG_INFO("mp_inject_real: RequestSessionStart returned");
    } else {
        // CLIENT: build a join descriptor matching the shape captured from a
        // real MCC MP join, then call Session_LoadJoinDescriptor. That spawns
        // the engine init thread which runs EngineStartup → handle-table CS
        // init → all the bootstrap we used to do by hand.
        char peer_addr_buf[64] = {};
        char peer_port_buf[16] = {};
        char peer_sid_buf[32]  = {};
        char peer_map_buf[16]  = {};
        GetEnvironmentVariableA("HALOX_PEER_ADDR",       peer_addr_buf, sizeof(peer_addr_buf));
        GetEnvironmentVariableA("HALOX_PEER_PORT",       peer_port_buf, sizeof(peer_port_buf));
        GetEnvironmentVariableA("HALOX_PEER_SESSION_ID", peer_sid_buf,  sizeof(peer_sid_buf));
        GetEnvironmentVariableA("HALOX_PEER_MAP_ID",     peer_map_buf,  sizeof(peer_map_buf));
        if (!peer_addr_buf[0] || !peer_port_buf[0] || !peer_sid_buf[0]) {
            CONSOLE_LOG_WARN("mp_inject_real: client side missing HALOX_PEER_{ADDR,PORT,SESSION_ID} — bailing");
            return 0;
        }

        uint64_t peer_session_id = strtoull(peer_sid_buf, nullptr, 10);
        uint32_t peer_map_id     = peer_map_buf[0] ? (uint32_t)atoi(peer_map_buf) : 0xD1;
        if (peer_session_id == 0) peer_session_id = 1;

        // Resolve the live MCC engine context. haloreach already stores it at
        // DAT_180c1a100 by the time Engine_NetworkMainLoopTick has fired once.
        void* engine_ctx = *reinterpret_cast<void**>(base + 0xC1A100);
        if (!engine_ctx) {
            CONSOLE_LOG_WARN("mp_inject_real: g_engineContext is null — bailing");
            return 0;
        }

        // Build descriptor mirroring captured layout. Zero everything else;
        // unknown fields haven't proven necessary so far. Fill what we know:
        memset(g_join_descriptor_blob, 0, k_join_descriptor_size);
        // +0x00..0x07: flags. Captured byte[0] == 0x00; rest of qword ~0x10000308.
        g_join_descriptor_blob[0]    = 0x00;
        g_join_descriptor_blob[1]    = 0x03;
        g_join_descriptor_blob[3]    = 0x10;
        g_join_descriptor_blob[4]    = 0x03;
        // +0x0C: game_mode = 3 (MP)
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0x0C) = 3;
        // +0x10/+0x14: map_id duplicated (captured: 0xD1 for the test session)
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0x10) = peer_map_id;
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0x14) = peer_map_id;
        // +0x18: 0x88880000 flag pattern (matches map.flags = 0x8888 in launch path)
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0x18) = 0x88880000;
        // +0x34: count = 1
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0x34) = 1;
        // +0x44: 0xFFFF (unknown — captured value)
        *reinterpret_cast<uint16_t*>(g_join_descriptor_blob + 0x44) = 0xFFFF;
        // +0x50: per-session GUID. Generate one; doesn't need cross-peer agreement.
        *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0x50) = peer_session_id ^ 0xDEADBEEFCAFEBABEULL;
        // +0x58, +0x60, +0xF8: peer/session identity, repeated. MUST match what
        // the host advertises. This is the field we used to wrongly encode as
        // (port<<32)|ipv4_be — it's actually an opaque 8-byte ID.
        *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0x58) = peer_session_id;
        *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0x60) = peer_session_id;
        *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0xF8) = peer_session_id;
        // +0xE8: count = 1
        *reinterpret_cast<uint32_t*>(g_join_descriptor_blob + 0xE8) = 1;
        // +0x2F8: passed by Session_JoinUpdateTick to Session_PrepareAndDispatchJoin
        // alongside the +0x50 GUID. The captured real call had non-zero data here.
        // Mirror the peer identity for now — we don't yet know its semantic.
        *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0x2F8) = peer_session_id;

        // Inline what Session_LoadJoinDescriptor would do, MINUS the thread
        // spawn at the end (CreateThread_Safe(LAB_18000DC78, 0)). The thread
        // it normally spawns runs Engine_InitializeAndRun → EngineStartup,
        // which re-initializes the handle table on an already-initialized
        // engine — corrupts in-progress session state and crashes other
        // subsystem threads on next handle-table access (FUN_18001398c+0x26
        // / FUN_18001251c+0x16 in our crash logs).
        //
        // The state-machine + hook combo (sm=9 forced below, and our
        // Player_CanUseEquipment hook) drives the join from the existing
        // engine tick without needing the spawn.
        CONSOLE_LOG_INFO("mp_inject_real: stamping descriptor inline (peer=%s:%s sid=%llu map=%u ec=%p)",
            peer_addr_buf, peer_port_buf,
            (unsigned long long)peer_session_id, (unsigned)peer_map_id, engine_ctx);

        // memcpy(&DAT_1824971A0, descriptor, 0x2BF30)
        memcpy(reinterpret_cast<void*>(base + 0x24971A0),
               g_join_descriptor_blob, k_join_descriptor_size);
        // DAT_184E38CE2 = 1 (descriptor-loaded flag)
        *reinterpret_cast<uint8_t*>(base + 0x4E38CE2) = 1;
        uint8_t flags0 = g_join_descriptor_blob[0];
        // if ((flags0 & 0x48) == 0x48) DAT_180C1A0BD = 1
        if ((flags0 & 0x48) == 0x48) *reinterpret_cast<uint8_t*>(base + 0xC1A0BD) = 1;
        // DAT_180D1ADE8 = *(u64*)(descriptor + 0x58)
        *reinterpret_cast<uint64_t*>(base + 0xD1ADE8) =
            *reinterpret_cast<uint64_t*>(g_join_descriptor_blob + 0x58);
        // if ((flags0 & 8) != 0) { DAT_180CFB5E8 = DAT_1824971F0 (=descriptor+0x50);
        //                          DAT_184E38CE1 = 1 }
        if (flags0 & 0x08) {
            *reinterpret_cast<uint64_t*>(base + 0xCFB5E8) =
                *reinterpret_cast<uint64_t*>(base + 0x24971F0);
            *reinterpret_cast<uint8_t*>(base + 0x4E38CE1) = 1;
        }
        // g_engineContext = engine_ctx
        *reinterpret_cast<void**>(base + 0xC1A100) = engine_ctx;
        // (skip CreateThread_Safe — would re-run Engine_InitializeAndRun)

        CONSOLE_LOG_INFO("mp_inject_real: descriptor inline-stamped (no thread spawn)");

        // Force the engine state machine into the JOIN state so
        // Session_JoinUpdateTick fires on the next ExternalLaunchStateMachineTick.
        // Without this, sm stays at whatever the in-game value was (e.g. 11)
        // and never enters state 9, so the descriptor we just loaded is ignored.
        // The hooked Player_CanUseEquipment(0) will then advance us 9 -> 0xB.
        *reinterpret_cast<uint32_t*>(base + 0xC1A114) = 9;
        // Reset the "join-initiated" latch so Session_JoinUpdateTick takes the
        // first-time branch and actually calls Session_PrepareAndDispatchJoin.
        *reinterpret_cast<uint8_t*> (base + 0xC1A0D0) = 0;
    }
    log_session_state(base, "worker-post-bootstrap");

    // Watchdog: 20s of state snapshots so we can see whether Session_JoinUpdateTick
    // and Player_CanUseEquipment(0) actually fire after our descriptor stamp.
    HANDLE wh = CreateThread(nullptr, 0, &real_watchdog_proc, nullptr, 0, nullptr);
    if (wh) {
        SetThreadPriority(wh, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(wh);
    }
    return 0;
}

// Hook on Engine_NetworkMainLoopTick. On the FIRST call, kick the bootstrap
// worker off-thread and return immediately so the engine keeps pumping.
static void hook_engine_net_main_loop_first_tick() {
    bool expected = false;
    if (g_real_init_fired.compare_exchange_strong(expected, true)) {
        HANDLE h = CreateThread(nullptr, 0, &real_bootstrap_worker, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    g_orig_engine_net_main_loop();
}

void mp_session_inject_real_apply(uint64_t peer_guid, bool is_host) {
    bool expected = false;
    if (!g_real_applied.compare_exchange_strong(expected, true)) return;

    HMODULE mod = haloreach_module();
    if (!mod) {
        CONSOLE_LOG_WARN("mp_inject_real: haloreach.dll not loaded — bailing");
        g_real_applied = false;
        return;
    }
    auto base = reinterpret_cast<uintptr_t>(mod);

    g_real_init_is_host = is_host;
    g_real_init_peer_guid = peer_guid;

    CONSOLE_LOG_INFO("mp_inject_real: scheduling first-tick init (host=%d, peer_guid=0x%016llX, base=%p)",
        (int)is_host, (unsigned long long)peer_guid, (void*)base);
    log_session_state(base, "pre");

    // Install the deferred hook. Engine memory + engine thread are ready by
    // the time Engine_NetworkMainLoopTick is first called.
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_Initialize failed");
        g_real_applied = false;
        return;
    }
    void* target = reinterpret_cast<void*>(base + k_rva_engine_net_main_loop);
    if (MH_CreateHook(target,
                      reinterpret_cast<void*>(&hook_engine_net_main_loop_first_tick),
                      reinterpret_cast<void**>(&g_orig_engine_net_main_loop)) != MH_OK) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_CreateHook(Engine_NetworkMainLoopTick) failed");
        g_real_applied = false;
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        CONSOLE_LOG_WARN("mp_inject_real: MH_EnableHook(Engine_NetworkMainLoopTick) failed");
        g_real_applied = false;
        return;
    }
    CONSOLE_LOG_INFO("mp_inject_real: hook armed @ Engine_NetworkMainLoopTick %p — init will run on first tick", target);
    (void)&real_state_dumper_proc; // keep symbol for diagnostics if re-enabled
}

void mp_session_inject_apply(uint64_t peer_guid) {
    bool expected = false;
    if (!g_applied.compare_exchange_strong(expected, true)) return;

    HMODULE mod = haloreach_module();
    if (!mod) {
        CONSOLE_LOG_WARN("mp_inject: haloreach.dll not loaded — bailing");
        g_applied = false;
        return;
    }
    auto base = reinterpret_cast<uintptr_t>(mod);
    CONSOLE_LOG_INFO("mp_inject: applying (haloreach base=%p, peer_guid=0x%016llX)",
        (void*)base, (unsigned long long)peer_guid);

    // Install hooks BEFORE stamping globals. The engine network thread is
    // already running by the time we get here — if we stamp globals first,
    // the engine can race into Session_ProcessNetworkAndTimers /
    // ProcessPendingChannelEvents / SessionConnection_UpdateState before
    // their no-op hooks are live, and crash on unpopulated state. With hooks
    // installed first, those functions become safe even before stamps land.
    install_is_in_game_state_hook(base);
    stamp_globals(base);
    inject_peer(base, peer_guid);

    CONSOLE_LOG_INFO("mp_inject: ready (state=2 gate set, mode=3, campaign=0, peer slot0 populated)");
}

static DWORD WINAPI pinning_thread_proc(LPVOID) {
    CONSOLE_LOG_INFO("mp_inject: pinning thread started (5ms cadence)");
    HMODULE mod = haloreach_module();
    if (!mod) return 0;
    auto base = reinterpret_cast<uintptr_t>(mod);

    while (true) {
        // Re-stamp the cheap globals every tick so libmcc/MCC overwrites
        // don't kick us out of MP state. The peer-table slot 0 is preserved
        // too — defensive against any code that NULLs it on session reset.
        // DAT_182a1fd40 is critical: Game_UpdateSessionAndNetworkState reads
        // it as a gate before touching session-ctx pointers. If MCC clears it
        // and we don't re-stamp before the next tick, the piVar22 read sees
        // NULL → AV at +0xBE.
        *reinterpret_cast<uint32_t*>(base + k_rva_global_subsystem_inited) = 1;
        *reinterpret_cast<uint32_t*>(base + k_rva_game_mode)         = 3;
        *reinterpret_cast<uint32_t*>(base + k_rva_campaign_gate)     = 0;
        *reinterpret_cast<uint8_t*> (base + k_rva_global_state_gate) = 1;
        *reinterpret_cast<uint32_t*>(base + k_rva_global_state_int)  = 2;
        // Re-stamp all 4 session-ctx slots and the in-game state field —
        // Game_UpdateSessionAndNetworkState overwrites the state; FUN_18038FAE8
        // needs it in [4,9] to return true. Keep all 4 slots pointing at our
        // buffer so the slot-iteration deref pattern stays valid.
        if (g_session_ctx_buffer) {
            auto buf = reinterpret_cast<uintptr_t>(g_session_ctx_buffer);
            // Re-stamp UNCONDITIONALLY (not just on NULL). MCC and the engine
            // may overwrite these slots on state transitions; a single missed
            // tick = AV at Game_UpdateSessionAndNetworkState +0xBE.
            auto* tbl = reinterpret_cast<uintptr_t*>(base + k_rva_session_ctx_ptr_global);
            for (int i = 0; i < 4; ++i) {
                tbl[i] = buf;
            }
            auto* fd30 = reinterpret_cast<uintptr_t*>(base + 0x2A1FD30);
            for (int i = 0; i < 3; ++i) {
                fd30[i] = buf;
            }
            *reinterpret_cast<uint32_t*>(buf + k_session_ctx_state_off) = 4;
        }
        if (g_peer_slot0_value) {
            auto* slot0 = reinterpret_cast<uint64_t*>(base + k_rva_session_struct + k_session_peer_slot0_off);
            *slot0 = g_peer_slot0_value;
        }
        g_pinning_writes++;
        Sleep(5);
    }
    return 0;
}

void mp_session_inject_start_pinning_thread() {
    bool expected = false;
    if (!g_pinning_started.compare_exchange_strong(expected, true)) return;
    HANDLE h = CreateThread(nullptr, 0, &pinning_thread_proc, nullptr, 0, nullptr);
    if (!h) {
        CONSOLE_LOG_WARN("mp_inject: pinning thread CreateThread failed (gle=%lu)", GetLastError());
        g_pinning_started = false;
        return;
    }
    SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(h);
}

void mp_session_inject_get_stats(s_mp_inject_stats* out) {
    if (!out) return;
    out->is_in_game_state_calls = g_calls_to_is_in_game_state;
    out->globals_pinned_writes  = g_pinning_writes;
    out->peer_pointer_at_slot0  = g_peer_slot0_value;
}

} // namespace halox::network
