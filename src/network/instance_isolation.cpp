#include "instance_isolation.h"

#include "../logging/logging.h"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace halox::network {

static std::atomic<bool> g_installed{false};
static std::string g_prefix_a;       // ASCII prefix, e.g. "haloxA_"
static std::wstring g_prefix_w;      // wide prefix

// Function pointer typedefs for the kernel32 named-object APIs we hook.
using PFN_CreateMutexW           = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCWSTR);
using PFN_CreateMutexA           = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
using PFN_CreateMutexExW         = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
using PFN_CreateMutexExA         = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCSTR,  DWORD, DWORD);
using PFN_OpenMutexW             = HANDLE (WINAPI*)(DWORD, BOOL, LPCWSTR);
using PFN_OpenMutexA             = HANDLE (WINAPI*)(DWORD, BOOL, LPCSTR);

using PFN_CreateEventW           = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR);
using PFN_CreateEventA           = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
using PFN_CreateEventExW         = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
using PFN_CreateEventExA         = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCSTR,  DWORD, DWORD);
using PFN_OpenEventW             = HANDLE (WINAPI*)(DWORD, BOOL, LPCWSTR);
using PFN_OpenEventA             = HANDLE (WINAPI*)(DWORD, BOOL, LPCSTR);

using PFN_CreateSemaphoreW       = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCWSTR);
using PFN_CreateSemaphoreA       = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR);
using PFN_OpenSemaphoreW         = HANDLE (WINAPI*)(DWORD, BOOL, LPCWSTR);
using PFN_OpenSemaphoreA         = HANDLE (WINAPI*)(DWORD, BOOL, LPCSTR);

using PFN_CreateFileMappingW     = HANDLE (WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
using PFN_CreateFileMappingA     = HANDLE (WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
using PFN_OpenFileMappingW       = HANDLE (WINAPI*)(DWORD, BOOL, LPCWSTR);
using PFN_OpenFileMappingA       = HANDLE (WINAPI*)(DWORD, BOOL, LPCSTR);

static PFN_CreateMutexW       o_CreateMutexW       = nullptr;
static PFN_CreateMutexA       o_CreateMutexA       = nullptr;
static PFN_CreateMutexExW     o_CreateMutexExW     = nullptr;
static PFN_CreateMutexExA     o_CreateMutexExA     = nullptr;
static PFN_OpenMutexW         o_OpenMutexW         = nullptr;
static PFN_OpenMutexA         o_OpenMutexA         = nullptr;
static PFN_CreateEventW       o_CreateEventW       = nullptr;
static PFN_CreateEventA       o_CreateEventA       = nullptr;
static PFN_CreateEventExW     o_CreateEventExW     = nullptr;
static PFN_CreateEventExA     o_CreateEventExA     = nullptr;
static PFN_OpenEventW         o_OpenEventW         = nullptr;
static PFN_OpenEventA         o_OpenEventA         = nullptr;
static PFN_CreateSemaphoreW   o_CreateSemaphoreW   = nullptr;
static PFN_CreateSemaphoreA   o_CreateSemaphoreA   = nullptr;
static PFN_OpenSemaphoreW     o_OpenSemaphoreW     = nullptr;
static PFN_OpenSemaphoreA     o_OpenSemaphoreA     = nullptr;
static PFN_CreateFileMappingW o_CreateFileMappingW = nullptr;
static PFN_CreateFileMappingA o_CreateFileMappingA = nullptr;
static PFN_OpenFileMappingW   o_OpenFileMappingW   = nullptr;
static PFN_OpenFileMappingA   o_OpenFileMappingA   = nullptr;

// Only prefix UNSCOPED names (no backslash). Names with a namespace scope
// like "Global\..." or "Local\..." or session-scoped paths are typically
// created by system DLLs (DXGI/DWM/ole32/ucrtbase/RPC) for cross-process
// coordination — munging those breaks system sync and the UI thread can
// stop pumping (white "Not Responding" window).
//
// The engine's own singleton names tend to be unscoped. If we later find a
// Reach singleton that *does* use Global\, we can extend this with an
// explicit allowlist of game-owned scoped names. Until then: keep it tight.
static const wchar_t* munge_w(const wchar_t* name, std::wstring& tmp) {
    if (!name || !*name) return name;
    if (wcschr(name, L'\\')) return name;  // scoped — leave alone
    tmp.clear();
    tmp.append(g_prefix_w);
    tmp.append(name);
    return tmp.c_str();
}

static const char* munge_a(const char* name, std::string& tmp) {
    if (!name || !*name) return name;
    if (strchr(name, '\\')) return name;
    tmp.clear();
    tmp.append(g_prefix_a);
    tmp.append(name);
    return tmp.c_str();
}

// ---------------------------------------------------------------- mutex
static HANDLE WINAPI h_CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL initial, LPCWSTR name) {
    std::wstring tmp;
    return o_CreateMutexW(sa, initial, munge_w(name, tmp));
}
static HANDLE WINAPI h_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL initial, LPCSTR name) {
    std::string tmp;
    return o_CreateMutexA(sa, initial, munge_a(name, tmp));
}
static HANDLE WINAPI h_CreateMutexExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access) {
    std::wstring tmp;
    return o_CreateMutexExW(sa, munge_w(name, tmp), flags, access);
}
static HANDLE WINAPI h_CreateMutexExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access) {
    std::string tmp;
    return o_CreateMutexExA(sa, munge_a(name, tmp), flags, access);
}
static HANDLE WINAPI h_OpenMutexW(DWORD access, BOOL inherit, LPCWSTR name) {
    std::wstring tmp;
    return o_OpenMutexW(access, inherit, munge_w(name, tmp));
}
static HANDLE WINAPI h_OpenMutexA(DWORD access, BOOL inherit, LPCSTR name) {
    std::string tmp;
    return o_OpenMutexA(access, inherit, munge_a(name, tmp));
}

// ---------------------------------------------------------------- event
static HANDLE WINAPI h_CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCWSTR name) {
    std::wstring tmp;
    return o_CreateEventW(sa, manual, initial, munge_w(name, tmp));
}
static HANDLE WINAPI h_CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL manual, BOOL initial, LPCSTR name) {
    std::string tmp;
    return o_CreateEventA(sa, manual, initial, munge_a(name, tmp));
}
static HANDLE WINAPI h_CreateEventExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD access) {
    std::wstring tmp;
    return o_CreateEventExW(sa, munge_w(name, tmp), flags, access);
}
static HANDLE WINAPI h_CreateEventExA(LPSECURITY_ATTRIBUTES sa, LPCSTR name, DWORD flags, DWORD access) {
    std::string tmp;
    return o_CreateEventExA(sa, munge_a(name, tmp), flags, access);
}
static HANDLE WINAPI h_OpenEventW(DWORD access, BOOL inherit, LPCWSTR name) {
    std::wstring tmp;
    return o_OpenEventW(access, inherit, munge_w(name, tmp));
}
static HANDLE WINAPI h_OpenEventA(DWORD access, BOOL inherit, LPCSTR name) {
    std::string tmp;
    return o_OpenEventA(access, inherit, munge_a(name, tmp));
}

// ------------------------------------------------------------ semaphore
static HANDLE WINAPI h_CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG init, LONG max, LPCWSTR name) {
    std::wstring tmp;
    return o_CreateSemaphoreW(sa, init, max, munge_w(name, tmp));
}
static HANDLE WINAPI h_CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG init, LONG max, LPCSTR name) {
    std::string tmp;
    return o_CreateSemaphoreA(sa, init, max, munge_a(name, tmp));
}
static HANDLE WINAPI h_OpenSemaphoreW(DWORD access, BOOL inherit, LPCWSTR name) {
    std::wstring tmp;
    return o_OpenSemaphoreW(access, inherit, munge_w(name, tmp));
}
static HANDLE WINAPI h_OpenSemaphoreA(DWORD access, BOOL inherit, LPCSTR name) {
    std::string tmp;
    return o_OpenSemaphoreA(access, inherit, munge_a(name, tmp));
}

// ---------------------------------------------------------- file mapping
static HANDLE WINAPI h_CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                          DWORD high, DWORD low, LPCWSTR name) {
    std::wstring tmp;
    return o_CreateFileMappingW(file, sa, prot, high, low, munge_w(name, tmp));
}
static HANDLE WINAPI h_CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                          DWORD high, DWORD low, LPCSTR name) {
    std::string tmp;
    return o_CreateFileMappingA(file, sa, prot, high, low, munge_a(name, tmp));
}
static HANDLE WINAPI h_OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name) {
    std::wstring tmp;
    return o_OpenFileMappingW(access, inherit, munge_w(name, tmp));
}
static HANDLE WINAPI h_OpenFileMappingA(DWORD access, BOOL inherit, LPCSTR name) {
    std::string tmp;
    return o_OpenFileMappingA(access, inherit, munge_a(name, tmp));
}

// --------------------------------------------------------------- helper
template <typename T>
static bool arm(const char* dll, const char* sym, T detour, T& orig) {
    HMODULE m = GetModuleHandleA(dll);
    if (!m) return false;
    void* target = GetProcAddress(m, sym);
    if (!target) return false;
    void* trampoline = nullptr;
    if (MH_CreateHook(target, reinterpret_cast<void*>(detour), &trampoline) != MH_OK) {
        CONSOLE_LOG_WARN("instance_isolation: MH_CreateHook(%s) failed", sym);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        CONSOLE_LOG_WARN("instance_isolation: MH_EnableHook(%s) failed", sym);
        return false;
    }
    orig = reinterpret_cast<T>(trampoline);
    return true;
}

void instance_isolation_install() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true)) return;

    // Opt-in. Hooking 20 kernel32 named-object APIs process-wide caused
    // accumulated freezes (MinHook trampolines on system-DLL hot paths
    // that pass unscoped names through). Default off until we identify a
    // specific game singleton that genuinely needs isolation, then we'll
    // switch to an explicit allowlist instead of intercepting everything.
    char on_buf[8] = {};
    GetEnvironmentVariableA("HALOX_ISOLATE", on_buf, sizeof(on_buf));
    if (on_buf[0] != '1') {
        CONSOLE_LOG_INFO("instance_isolation: disabled (set HALOX_ISOLATE=1 to enable)");
        return;
    }

    char inst_buf[64] = {};
    DWORD got = GetEnvironmentVariableA("HALOX_INSTANCE", inst_buf, sizeof(inst_buf));
    char prefix[96];
    if (got == 0 || !inst_buf[0]) {
        snprintf(prefix, sizeof(prefix), "haloxP%lu_", GetCurrentProcessId());
    } else {
        snprintf(prefix, sizeof(prefix), "halox%s_", inst_buf);
    }
    g_prefix_a = prefix;
    g_prefix_w.assign(g_prefix_a.begin(), g_prefix_a.end());

    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        CONSOLE_LOG_WARN("instance_isolation: MH_Initialize failed");
        g_installed = false;
        return;
    }

    int armed = 0;
    armed += arm("kernel32.dll", "CreateMutexW",        &h_CreateMutexW,        o_CreateMutexW);
    armed += arm("kernel32.dll", "CreateMutexA",        &h_CreateMutexA,        o_CreateMutexA);
    armed += arm("kernel32.dll", "CreateMutexExW",      &h_CreateMutexExW,      o_CreateMutexExW);
    armed += arm("kernel32.dll", "CreateMutexExA",      &h_CreateMutexExA,      o_CreateMutexExA);
    armed += arm("kernel32.dll", "OpenMutexW",          &h_OpenMutexW,          o_OpenMutexW);
    armed += arm("kernel32.dll", "OpenMutexA",          &h_OpenMutexA,          o_OpenMutexA);

    armed += arm("kernel32.dll", "CreateEventW",        &h_CreateEventW,        o_CreateEventW);
    armed += arm("kernel32.dll", "CreateEventA",        &h_CreateEventA,        o_CreateEventA);
    armed += arm("kernel32.dll", "CreateEventExW",      &h_CreateEventExW,      o_CreateEventExW);
    armed += arm("kernel32.dll", "CreateEventExA",      &h_CreateEventExA,      o_CreateEventExA);
    armed += arm("kernel32.dll", "OpenEventW",          &h_OpenEventW,          o_OpenEventW);
    armed += arm("kernel32.dll", "OpenEventA",          &h_OpenEventA,          o_OpenEventA);

    armed += arm("kernel32.dll", "CreateSemaphoreW",    &h_CreateSemaphoreW,    o_CreateSemaphoreW);
    armed += arm("kernel32.dll", "CreateSemaphoreA",    &h_CreateSemaphoreA,    o_CreateSemaphoreA);
    armed += arm("kernel32.dll", "OpenSemaphoreW",      &h_OpenSemaphoreW,      o_OpenSemaphoreW);
    armed += arm("kernel32.dll", "OpenSemaphoreA",      &h_OpenSemaphoreA,      o_OpenSemaphoreA);

    armed += arm("kernel32.dll", "CreateFileMappingW",  &h_CreateFileMappingW,  o_CreateFileMappingW);
    armed += arm("kernel32.dll", "CreateFileMappingA",  &h_CreateFileMappingA,  o_CreateFileMappingA);
    armed += arm("kernel32.dll", "OpenFileMappingW",    &h_OpenFileMappingW,    o_OpenFileMappingW);
    armed += arm("kernel32.dll", "OpenFileMappingA",    &h_OpenFileMappingA,    o_OpenFileMappingA);

    CONSOLE_LOG_INFO("instance_isolation: prefix=\"%s\" hooks_armed=%d/20", g_prefix_a.c_str(), armed);
}

} // namespace halox::network
