#include "engine_context_logger.h"

#include "../game/game_manager.h"
#include "../logging/logging.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>

namespace halox::network {

// Upper bound on slot count we instrument. MCC's c_engine_context vtable has
// ~190 slots per the haloreach capture; 256 gives plenty of headroom and is
// cheap to instantiate.
static constexpr int    k_max_slots  = 256;
static constexpr size_t k_vt_bytes   = k_max_slots * sizeof(void*);

static std::atomic<bool>     g_inited{false};
static void**                g_orig_vt   = nullptr;   // captured originals
static void**                g_logger_vt = nullptr;   // our shim
static std::atomic<uint32_t> g_call_count[k_max_slots];

// Per-slot trampoline. Logs the first few calls (then throttles to powers of
// 2) and tail-chains to the captured original. We use uint64_t for all four
// fastcall slots — args 5+ on the stack are NOT forwarded cleanly because
// the C++ epilogue rebuilds the frame; if a slot turns out to take >4 args
// and chaining breaks, we'll see it in the log and can switch that slot to
// a hand-rolled asm thunk. Float/SSE returns are similarly best-effort.
template <int N>
static uint64_t __fastcall logger_stub(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint32_t c = g_call_count[N].fetch_add(1, std::memory_order_relaxed) + 1;
    if (c <= 4 || c == 16 || c == 64 || c == 256 || c == 1024) {
        CONSOLE_LOG_INFO(
            "ec_log: slot[%3d/0x%02X] call#%u a1=0x%llX a2=0x%llX a3=0x%llX a4=0x%llX",
            N, N, c,
            (unsigned long long)a1, (unsigned long long)a2,
            (unsigned long long)a3, (unsigned long long)a4);
    }
    void* fn = g_orig_vt[N];
    if (!fn) return 0;
    using FnT = uint64_t(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
    return reinterpret_cast<FnT>(fn)(a1, a2, a3, a4);
}

// Compile-time fan-out: instantiate logger_stub<0..k_max_slots-1> and write
// each function's address into the corresponding vtable slot.
template <size_t... Ns>
static void fill_logger_vt_impl(void** vt, std::index_sequence<Ns...>) {
    ((vt[Ns] = reinterpret_cast<void*>(&logger_stub<Ns>)), ...);
}
static void fill_logger_vt(void** vt) {
    fill_logger_vt_impl(vt, std::make_index_sequence<k_max_slots>{});
}

bool engine_context_logger_install() {
    bool expected = false;
    if (!g_inited.compare_exchange_strong(expected, true)) {
        CONSOLE_LOG_INFO("ec_log: already installed — skipping");
        return false;
    }

    void** obj_vt_slot = reinterpret_cast<void**>(&::g_game_manager);
    void*  current_vt  = *obj_vt_slot;
    if (!current_vt) {
        CONSOLE_LOG_WARN("ec_log: g_game_manager vtable is NULL — refusing to install");
        g_inited = false;
        return false;
    }

    g_orig_vt = static_cast<void**>(VirtualAlloc(
        nullptr, k_vt_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    g_logger_vt = static_cast<void**>(VirtualAlloc(
        nullptr, k_vt_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_orig_vt || !g_logger_vt) {
        CONSOLE_LOG_WARN("ec_log: VirtualAlloc(%zu) failed (gle=%lu)",
            k_vt_bytes, GetLastError());
        g_inited = false;
        return false;
    }

    // Snapshot the existing vtable contents wholesale. We may snapshot past
    // the actual class vtable bounds — that's the same thing engine_context_shim
    // does and works fine in practice (memory there is well-behaved).
    memcpy(g_orig_vt, current_vt, k_vt_bytes);

    fill_logger_vt(g_logger_vt);

    InterlockedExchangePointer(reinterpret_cast<volatile PVOID*>(obj_vt_slot), g_logger_vt);

    CONSOLE_LOG_INFO("ec_log: vtable swapped (orig=%p logger=%p obj=%p, %d slots wired)",
        current_vt, (void*)g_logger_vt, (void*)obj_vt_slot, k_max_slots);
    return true;
}

void engine_context_logger_dump_summary() {
    if (!g_inited.load()) {
        CONSOLE_LOG_INFO("ec_log: dump_summary called but logger not installed");
        return;
    }
    int observed = 0;
    uint32_t total = 0;
    for (int i = 0; i < k_max_slots; i++) {
        uint32_t c = g_call_count[i].load(std::memory_order_relaxed);
        if (c > 0) {
            CONSOLE_LOG_INFO("ec_log: SUMMARY slot[%3d/0x%02X] total_calls=%u", i, i, c);
            observed++;
            total += c;
        }
    }
    CONSOLE_LOG_INFO("ec_log: SUMMARY observed_slots=%d / %d, total_calls=%u",
        observed, k_max_slots, total);
}

} // namespace halox::network
