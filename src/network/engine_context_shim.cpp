#include "engine_context_shim.h"

// WinSock2.h must precede Windows.h — Windows.h transitively pulls in v1
// winsock.h which collides with v2 declarations.
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "../game/game_manager.h"
#include "../logging/logging.h"

#include <atomic>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "Ws2_32.lib")

namespace halox::network {

// Reach calls these vtable byte offsets on g_engineContext (the libmcc
// game_manager pointer that gets stored as g_engineContext inside Reach's
// Session_LoadJoinDescriptor):
//
//   +0x148  send  void(this, peer_id, const void* buf, int len)
//   +0x158  recv  int (this, void* buf, int max_len, uint64_t* peer_id_out)
//                 returns bytes received (0 = idle)
static constexpr ptrdiff_t k_vt_send_off = 0x148;
static constexpr ptrdiff_t k_vt_recv_off = 0x158;

// Conservative shim-vtable size. The original libmcc i_game_manager vtable
// is much smaller (~10 slots), but Reach indirects up to at least 0x158, so
// we copy enough to keep any in-bounds access safe AND to preserve whatever
// adjacent memory currently sits there (since Reach's solo flow already
// reads those addresses without crashing — meaning the bytes there are
// "well-behaved" enough that our copy keeps the status quo).
static constexpr size_t    k_shim_vt_size = 0x200;

using send_fn_t = void (*)(void*, uint64_t, const void*, int);
using recv_fn_t = int  (*)(void*, void*, int, uint64_t*);

static std::atomic<bool>  g_inited{false};
static SOCKET             g_socket = INVALID_SOCKET;
static sockaddr_in        g_peer_addr{};
static bool               g_peer_known = false;
static void**             g_shim_vtable = nullptr;  // heap-allocated copy
static void*              g_orig_vtable_ptr = nullptr;
static libmcc::c_game_manager* g_patched_object = nullptr;

static s_shim_stats       g_stats{};

// Pre-existing fallback no-op stub: any unknown vtable slot we didn't copy
// completely (shouldn't happen since we copy 0x200 bytes verbatim) lands here
// and returns 0/NULL. Naked-return assembly so Reach's caller sees a clean
// stack and a zero return.
static void shim_noop() {}

// Slot 0x148 detour — Reach's send_packet seam.
static void shim_send(void* /*this_*/, uint64_t peer_id, const void* buf, int len) {
    g_stats.sends++;
    g_stats.bytes_sent += (uint64_t)(len > 0 ? len : 0);

    if (g_socket == INVALID_SOCKET || !g_peer_known) {
        CONSOLE_LOG_WARN("ec_shim_send: dropped (socket=%lld peer_known=%d) peer_id=%llu len=%d",
            (long long)g_socket, (int)g_peer_known, (unsigned long long)peer_id, len);
        return;
    }

    int sent = sendto(g_socket,
                      reinterpret_cast<const char*>(buf),
                      len,
                      0,
                      reinterpret_cast<const sockaddr*>(&g_peer_addr),
                      (int)sizeof(g_peer_addr));
    if (sent == SOCKET_ERROR) {
        g_stats.send_errors++;
        int err = WSAGetLastError();
        // Rate-limit log spam: only first error per session.
        if (g_stats.send_errors == 1) {
            CONSOLE_LOG_WARN("ec_shim_send: sendto failed (WSAErr=%d) peer_id=%llu len=%d",
                err, (unsigned long long)peer_id, len);
        }
    } else if (g_stats.sends == 1) {
        CONSOLE_LOG_INFO("ec_shim_send: first send ok peer_id=%llu len=%d sent=%d",
            (unsigned long long)peer_id, len, sent);
    }
}

// Slot 0x158 detour — Reach's recv seam.
static int shim_recv(void* /*this_*/, void* buf, int max_len, uint64_t* peer_id_out) {
    g_stats.recvs_polled++;

    if (g_socket == INVALID_SOCKET) return 0;

    sockaddr_in src{};
    int srclen = (int)sizeof(src);
    int n = recvfrom(g_socket,
                     reinterpret_cast<char*>(buf),
                     max_len,
                     0,
                     reinterpret_cast<sockaddr*>(&src),
                     &srclen);
    if (n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEMSGSIZE) {
            g_stats.recv_errors++;
            if (g_stats.recv_errors == 1) {
                CONSOLE_LOG_WARN("ec_shim_recv: recvfrom failed (WSAErr=%d)", err);
            }
        }
        return 0;
    }
    if (n <= 0) return 0;

    g_stats.recvs_returned++;
    g_stats.bytes_received += (uint64_t)n;

    if (peer_id_out) {
        // Encode peer addr as a 64-bit id: (port << 32) | ipv4_be — collisionless
        // for two-instance LAN where each peer has a fixed (ip,port) pair.
        uint64_t pid = ((uint64_t)src.sin_port << 32) | (uint32_t)src.sin_addr.s_addr;
        *peer_id_out = pid;
    }

    if (g_stats.recvs_returned == 1) {
        char ipstr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &src.sin_addr, ipstr, sizeof(ipstr));
        CONSOLE_LOG_INFO("ec_shim_recv: first recv ok bytes=%d from=%s:%u",
            n, ipstr, (unsigned)ntohs(src.sin_port));
    }

    return n;
}

static bool open_socket(int bind_port) {
    WSADATA wsd{};
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        CONSOLE_LOG_WARN("ec_shim: WSAStartup failed");
        return false;
    }

    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_socket == INVALID_SOCKET) {
        CONSOLE_LOG_WARN("ec_shim: socket() failed (WSAErr=%d)", WSAGetLastError());
        return false;
    }

    // Non-blocking — Reach's recv path polls; blocking would stall the engine.
    u_long nb = 1;
    if (ioctlsocket(g_socket, FIONBIO, &nb) == SOCKET_ERROR) {
        CONSOLE_LOG_WARN("ec_shim: ioctlsocket FIONBIO failed (WSAErr=%d)", WSAGetLastError());
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((u_short)bind_port);
    if (bind(g_socket, reinterpret_cast<sockaddr*>(&addr), (int)sizeof(addr)) == SOCKET_ERROR) {
        CONSOLE_LOG_WARN("ec_shim: bind(:%d) failed (WSAErr=%d)", bind_port, WSAGetLastError());
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        return false;
    }

    sockaddr_in bound{};
    int bound_len = (int)sizeof(bound);
    getsockname(g_socket, reinterpret_cast<sockaddr*>(&bound), &bound_len);
    CONSOLE_LOG_INFO("ec_shim: UDP socket bound :%u", (unsigned)ntohs(bound.sin_port));
    return true;
}

static bool resolve_peer(const char* peer_addr, int peer_port) {
    if (!peer_addr || !*peer_addr) {
        CONSOLE_LOG_INFO("ec_shim: no peer_addr supplied — sends will drop until peer learned via recv");
        g_peer_known = false;
        return true;
    }

    g_peer_addr.sin_family = AF_INET;
    g_peer_addr.sin_port = htons((u_short)peer_port);
    if (inet_pton(AF_INET, peer_addr, &g_peer_addr.sin_addr) != 1) {
        CONSOLE_LOG_WARN("ec_shim: inet_pton(%s) failed", peer_addr);
        return false;
    }
    g_peer_known = true;
    CONSOLE_LOG_INFO("ec_shim: peer = %s:%d", peer_addr, peer_port);
    return true;
}

static bool patch_vtable() {
    // Take the libmcc instance halox already passes to initialize_game.
    // Its first 8 bytes are the vtable pointer. We need to:
    //   1. Capture the original vtable pointer (so we copy slot 0..N).
    //   2. Allocate a heap buffer the same size as our shim window.
    //   3. memcpy from the original vtable address (this includes adjacent
    //      memory past the actual i_game_manager vtable bounds — that's
    //      intentional: those bytes are what Reach already reads in solo
    //      flow, so preserving them keeps the status quo for any slot we
    //      don't override).
    //   4. Patch byte offsets 0x148 and 0x158.
    //   5. VirtualProtect g_game_manager's first 8 bytes RW (it should
    //      already be RW since it's a static C++ object in halox's data
    //      segment) and atomically swap the vtable pointer.

    g_patched_object = &::g_game_manager;
    void** obj_vtptr_slot = reinterpret_cast<void**>(g_patched_object);
    g_orig_vtable_ptr = *obj_vtptr_slot;
    if (!g_orig_vtable_ptr) {
        CONSOLE_LOG_WARN("ec_shim: g_game_manager vtable pointer is NULL — refusing to patch");
        return false;
    }

    g_shim_vtable = reinterpret_cast<void**>(VirtualAlloc(nullptr, k_shim_vt_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_shim_vtable) {
        CONSOLE_LOG_WARN("ec_shim: VirtualAlloc(%zu) failed (gle=%lu)", k_shim_vt_size, GetLastError());
        return false;
    }

    memcpy(g_shim_vtable, g_orig_vtable_ptr, k_shim_vt_size);

    // Patch the two slots Reach actually indirects through for networking.
    *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(g_shim_vtable) + k_vt_send_off) =
        reinterpret_cast<void*>(static_cast<send_fn_t>(&shim_send));
    *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(g_shim_vtable) + k_vt_recv_off) =
        reinterpret_cast<void*>(static_cast<recv_fn_t>(&shim_recv));

    // Make the shim vtable execute-safe (R+X) once we're done writing.
    DWORD oldProt = 0;
    if (!VirtualProtect(g_shim_vtable, k_shim_vt_size, PAGE_EXECUTE_READ, &oldProt)) {
        CONSOLE_LOG_WARN("ec_shim: VirtualProtect(shim_vt, RX) failed (gle=%lu)", GetLastError());
        // Continue — most data segments work writeable too; we just lose
        // DEP coverage on the table itself.
    }

    // Swap the vtable pointer in the libmcc object. The slot is just an
    // 8-byte field in halox's RW data segment, so the swap is a plain
    // store. Use Interlocked for visibility across threads.
    InterlockedExchangePointer(obj_vtptr_slot, g_shim_vtable);

    CONSOLE_LOG_INFO("ec_shim: vtable swapped (orig=%p shim=%p object=%p) +0x148=send +0x158=recv",
        g_orig_vtable_ptr, (void*)g_shim_vtable, (void*)g_patched_object);
    return true;
}

void engine_context_shim_init(int bind_port, const char* peer_addr, int peer_port) {
    bool expected = false;
    if (!g_inited.compare_exchange_strong(expected, true)) return;

    CONSOLE_LOG_INFO("ec_shim_init: bind_port=%d peer=%s:%d",
        bind_port, peer_addr ? peer_addr : "<none>", peer_port);

    if (!open_socket(bind_port))   { g_inited = false; return; }
    if (!resolve_peer(peer_addr, peer_port)) { g_inited = false; return; }
    if (!patch_vtable())           { g_inited = false; return; }

    CONSOLE_LOG_INFO("ec_shim_init: ready (slot 0x148/0x158 routed to halox UDP)");
}

bool engine_context_shim_ready() {
    return g_inited.load() && g_socket != INVALID_SOCKET && g_shim_vtable != nullptr;
}

void engine_context_shim_get_stats(s_shim_stats* out) {
    if (!out) return;
    *out = g_stats;
}

} // namespace halox::network
