#include "mcc_network_bridge.h"

#include "../logging/logging.h"

#include <Windows.h>
#include <cstdint>

namespace halox::network {

// MCC RVAs (image base 0x140000000 in Ghidra → these are file-relative).
// Source: ../../<ReverseMe handoff notes>:
//   0x1EA298  NetworkStats_RecordOutgoingMessage trampoline
//   0x1EA334  NetworkStats_RecordMessageReceive trampoline
//   0x473744  NetworkSession_GetCurrent
//   0x474244  NetworkStats_RecordMessageSend (real)
//   0x4743D0  NetworkStats_RecordMessageReceive (real)
static constexpr uint32_t k_rva_send_trampoline       = 0x1EA298;
static constexpr uint32_t k_rva_send_trampoline_alt   = 0x1EA2E4;
static constexpr uint32_t k_rva_recv_trampoline       = 0x1EA334;
static constexpr uint32_t k_rva_session_get_current   = 0x473744;
static constexpr uint32_t k_rva_record_send           = 0x474244;
static constexpr uint32_t k_rva_record_recv           = 0x4743D0;

// MCC trampoline ABIs as RE'd from Ghidra.
//
// NetworkStats_RecordOutgoingMessage:
//   void __fastcall(uint32_t peer_id, const void* buf, uint32_t len, uint32_t type);
// NetworkStats_RecordMessageReceive trampoline:
//   int  __fastcall(void* buf, uint32_t len, uint32_t* out_peer);
//   (return is bytes received; 0 = idle; the trampoline early-outs to 0
//    when NetworkSession_GetCurrent returns NULL)
// NetworkSession_GetCurrent:
//   void* __fastcall();
using t_send_tramp     = void (*)(uint32_t peer_id, const void* buf, uint32_t len, uint32_t type);
using t_recv_tramp     = int  (*)(void* buf, uint32_t len, uint32_t* out_peer);
using t_session_get    = void* (*)();
using t_record_send    = void (*)(void* session, uint32_t peer_id, const void* buf, uint32_t len, uint32_t internal_flag, uint32_t type);
using t_record_recv    = int  (*)(void* session, void* buf, uint32_t len, uint32_t* out_peer);

// Resolved pointers. Stay null until init runs successfully.
static HMODULE         g_halonetlayer_dll = nullptr;
static HMODULE         g_snl_dll          = nullptr;
static HMODULE         g_mcc_image        = nullptr;  // datafile mapping
static t_send_tramp    g_send_tramp       = nullptr;
static t_recv_tramp    g_recv_tramp       = nullptr;
static t_session_get   g_session_get      = nullptr;
static t_record_send   g_record_send      = nullptr;
static t_record_recv   g_record_recv      = nullptr;
static bool            g_ready            = false;
static bool            g_init_attempted   = false;

// SEH-wrap a call that might fault if MCC offsets drift between patches or
// if a resolved pointer points into an unrelocated section. Returns true on
// success (no exception); on AV logs and returns false. Mirrors the
// `seh_call` helper in game_instance_manager.launch.cpp.
template <typename Fn>
static bool seh_call(const char* name, Fn&& fn) {
    __try {
        fn();
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        CONSOLE_LOG_ERROR(
            "mcc_bridge: SEH 0x%08lX in '%s' — likely an unrelocated "
            "DATAFILE pointer or a stale RVA. See limitations note in "
            "mcc_network_bridge.h.",
            code, name);
        return false;
    }
}

// Resolve an RVA against a DATAFILE-mapped image. The mapping has the
// HMODULE's low bits set (LOAD_LIBRARY_AS_DATAFILE OR's bit 0); strip
// them before pointer arithmetic.
static void* rva_to_ptr(HMODULE datafile, uint32_t rva) {
    auto base = reinterpret_cast<uintptr_t>(datafile) & ~(uintptr_t)3;
    return reinterpret_cast<void*>(base + rva);
}

void mcc_bridge_init() {
    if (g_init_attempted) {
        return; // idempotent
    }
    g_init_attempted = true;

    CONSOLE_LOG_INFO("mcc_bridge_init: starting");

    // 1. halonetworklayer_ship.dll — session/driver vtable provider.
    //    cwd is MCC content root by this point (set in initialize_module).
    g_halonetlayer_dll = LoadLibraryW(L"MCC\\Binaries\\Win64\\halonetworklayer_ship.dll");
    if (g_halonetlayer_dll) {
        CONSOLE_LOG_INFO("mcc_bridge_init: halonetworklayer_ship.dll loaded @ %p", g_halonetlayer_dll);
    } else {
        CONSOLE_LOG_WARN("mcc_bridge_init: LoadLibraryW(halonetworklayer_ship.dll) failed: %lu", GetLastError());
    }

    // 2. simplenetworklibrary-x64-release.dll — raw transport beneath halonetworklayer.
    g_snl_dll = LoadLibraryW(L"MCC\\Binaries\\Win64\\simplenetworklibrary-x64-release.dll");
    if (g_snl_dll) {
        CONSOLE_LOG_INFO("mcc_bridge_init: simplenetworklibrary-x64-release.dll loaded @ %p", g_snl_dll);
    } else {
        CONSOLE_LOG_WARN("mcc_bridge_init: LoadLibraryW(simplenetworklibrary-x64-release.dll) failed: %lu", GetLastError());
    }

    // 3. MCC-Win64-Shipping.exe — opened as DATAFILE so DllMain / WinMain are
    //    NOT executed (the EXE entry-point would attempt to bootstrap MCC's
    //    full subsystem, which is unsafe inside halox's process). LOAD_LIBRARY
    //    _AS_IMAGE_RESOURCE additionally tells the loader to lay sections out
    //    as on disk (no relocations, no IAT fixups). This is enough to read
    //    bytes / disassemble offsets from within halox, but is NOT safe for
    //    actual code execution — see limitations in the header.
    constexpr DWORD k_mcc_load_flags =
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE |
        LOAD_LIBRARY_AS_IMAGE_RESOURCE;
    g_mcc_image = LoadLibraryExW(L"MCC\\Binaries\\Win64\\MCC-Win64-Shipping.exe", nullptr, k_mcc_load_flags);
    if (g_mcc_image) {
        CONSOLE_LOG_INFO("mcc_bridge_init: MCC-Win64-Shipping.exe mapped (datafile) @ %p", g_mcc_image);
    } else {
        CONSOLE_LOG_ERROR("mcc_bridge_init: LoadLibraryExW(MCC-Win64-Shipping.exe) failed: %lu", GetLastError());
    }

    // 4. Resolve the five RVAs. Each failure is logged individually so a
    //    silent build skew doesn't go unnoticed.
    if (g_mcc_image) {
        g_send_tramp  = reinterpret_cast<t_send_tramp> (rva_to_ptr(g_mcc_image, k_rva_send_trampoline));
        g_recv_tramp  = reinterpret_cast<t_recv_tramp> (rva_to_ptr(g_mcc_image, k_rva_recv_trampoline));
        g_session_get = reinterpret_cast<t_session_get>(rva_to_ptr(g_mcc_image, k_rva_session_get_current));
        g_record_send = reinterpret_cast<t_record_send>(rva_to_ptr(g_mcc_image, k_rva_record_send));
        g_record_recv = reinterpret_cast<t_record_recv>(rva_to_ptr(g_mcc_image, k_rva_record_recv));

        if (!g_send_tramp)  CONSOLE_LOG_ERROR("mcc_bridge_init: send_tramp RVA 0x%X resolved to null", k_rva_send_trampoline);
        if (!g_recv_tramp)  CONSOLE_LOG_ERROR("mcc_bridge_init: recv_tramp RVA 0x%X resolved to null", k_rva_recv_trampoline);
        if (!g_session_get) CONSOLE_LOG_ERROR("mcc_bridge_init: session_get RVA 0x%X resolved to null", k_rva_session_get_current);
        if (!g_record_send) CONSOLE_LOG_ERROR("mcc_bridge_init: record_send RVA 0x%X resolved to null", k_rva_record_send);
        if (!g_record_recv) CONSOLE_LOG_ERROR("mcc_bridge_init: record_recv RVA 0x%X resolved to null", k_rva_record_recv);

        CONSOLE_LOG_INFO(
            "mcc_bridge_init: resolved send=%p recv=%p session=%p rsend=%p rrecv=%p (alt_send_rva=0x%X)",
            (void*)g_send_tramp, (void*)g_recv_tramp, (void*)g_session_get,
            (void*)g_record_send, (void*)g_record_recv, k_rva_send_trampoline_alt);
    }

    g_ready = (g_halonetlayer_dll != nullptr) &&
              (g_snl_dll != nullptr) &&
              (g_mcc_image != nullptr) &&
              (g_send_tramp != nullptr) &&
              (g_recv_tramp != nullptr) &&
              (g_session_get != nullptr) &&
              (g_record_send != nullptr) &&
              (g_record_recv != nullptr);

    if (g_ready) {
        CONSOLE_LOG_INFO("mcc_bridge_init: ready (research scaffold — see header for current invocation limits)");
    } else {
        CONSOLE_LOG_WARN("mcc_bridge_init: NOT ready — bridge calls will short-circuit");
    }
}

bool mcc_bridge_ready() {
    return g_ready;
}

void mcc_bridge_send(uint32_t peer_id,
                     const void* buf,
                     uint32_t len,
                     uint32_t type) {
    if (!g_ready || !g_send_tramp) {
        return;
    }
    // Cheap pre-check: if there's no live NetworkSession, the underlying
    // RecordMessageSend would AV trying to take its critsec at +0x160.
    // The send trampoline at +0x1EA298 doesn't itself null-check — it
    // would dispatch (*(session+8))->slot_0x20 with NULL `this`. So we
    // mirror the recv-side early-out manually.
    void* session = nullptr;
    if (!seh_call("session_get_current(send)", [&] { session = g_session_get(); })) {
        return;
    }
    if (!session) {
        return; // no session, drop silently
    }
    seh_call("send_tramp", [&] { g_send_tramp(peer_id, buf, len, type); });
}

int mcc_bridge_recv(void* buf, uint32_t len, uint32_t* out_peer) {
    if (!g_ready || !g_recv_tramp || !g_session_get) {
        return -2;
    }
    // Recv trampoline is documented to early-out to 0 when GetCurrent returns
    // NULL, but we still pre-check here so that an invalid singleton (e.g.
    // garbage from an unrelocated image) shows up as -1 rather than a fault.
    void* session = nullptr;
    if (!seh_call("session_get_current(recv)", [&] { session = g_session_get(); })) {
        return -3;
    }
    if (!session) {
        return -1;
    }
    int rc = 0;
    if (!seh_call("recv_tramp", [&] { rc = g_recv_tramp(buf, len, out_peer); })) {
        return -3;
    }
    return rc;
}

} // namespace halox::network
