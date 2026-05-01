#include "instance_discovery.h"

#include <Windows.h>
#include <combaseapi.h>

#include "../logging/logging.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#pragma comment(lib, "Ole32.lib")

namespace halox::network {

namespace fs = std::filesystem;

static std::atomic<bool> g_inited{false};
static std::atomic<bool> g_stop{false};

static std::string g_self_uuid;
static std::string g_self_label;
static std::string g_self_hostname;
static std::string g_self_peer_addr;
static uint32_t    g_self_pid = 0;
static int         g_self_bind_port = 0;
static int         g_self_peer_port = 0;
static uint64_t    g_self_session_identity = 0;
static fs::path    g_discovery_dir;
static fs::path    g_self_heartbeat_path;

// Session state — what we're currently hosting. Mutated by the launch path.
static std::mutex g_session_mutex;
static int        g_session_module = -1;
static int        g_session_mode = -1;
static int        g_session_map_id = -1;
static int        g_session_hopper_game_variant = -1;
static int        g_session_hopper_map_variant = -1;
static int        g_session_difficulty = -1;
static std::string g_session_map_name;
static bool       g_session_in_game = false;

// Cached scan results — read by the UI thread, written by the heartbeat thread.
static std::mutex                   g_list_mutex;
static std::vector<s_instance_info> g_list;
static std::atomic<bool>            g_refresh_kick{false};

static uint64_t now_ms() {
    return (uint64_t)GetTickCount64();
}

static std::string make_uuid() {
    GUID g{};
    if (CoCreateGuid(&g) != S_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "halox-%lu-%llu",
            (unsigned long)GetCurrentProcessId(),
            (unsigned long long)now_ms());
        return buf;
    }
    char buf[64];
    snprintf(buf, sizeof(buf),
        "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        (unsigned long)g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

static fs::path discovery_directory() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    fs::path dir = fs::path(tmp) / L"halox-instances";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

static std::string get_hostname() {
    char host[256] = {};
    DWORD n = (DWORD)sizeof(host);
    if (GetComputerNameA(host, &n)) return host;
    return "unknown";
}

static int read_env_int(const char* key, int default_val) {
    char buf[64] = {};
    DWORD n = GetEnvironmentVariableA(key, buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return default_val;
    return atoi(buf);
}

static std::string read_env_str(const char* key, const char* default_val) {
    char buf[256] = {};
    DWORD n = GetEnvironmentVariableA(key, buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return default_val ? default_val : "";
    return buf;
}

static void write_heartbeat() {
    std::ostringstream os;
    {
        std::lock_guard<std::mutex> lk(g_session_mutex);
        os << "uuid=" << g_self_uuid << "\n"
           << "pid=" << g_self_pid << "\n"
           << "bind_port=" << g_self_bind_port << "\n"
           << "peer_addr=" << g_self_peer_addr << "\n"
           << "peer_port=" << g_self_peer_port << "\n"
           << "hostname=" << g_self_hostname << "\n"
           << "label=" << g_self_label << "\n"
           << "timestamp_ms=" << now_ms() << "\n"
           << "in_game=" << (g_session_in_game ? 1 : 0) << "\n"
           << "module=" << g_session_module << "\n"
           << "mode=" << g_session_mode << "\n"
           << "map_id=" << g_session_map_id << "\n"
           << "hopper_game_variant=" << g_session_hopper_game_variant << "\n"
           << "hopper_map_variant=" << g_session_hopper_map_variant << "\n"
           << "difficulty=" << g_session_difficulty << "\n"
           << "map_name=" << g_session_map_name << "\n"
           << "session_identity=" << g_self_session_identity << "\n";
    }

    fs::path tmp = g_self_heartbeat_path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return;
        f << os.str();
    }
    std::error_code ec;
    fs::rename(tmp, g_self_heartbeat_path, ec);
    if (ec) {
        // Atomic rename failed (Windows file locks etc) — fall back to a
        // direct write. Slightly less torn-read-safe but acceptable.
        fs::remove(tmp, ec);
        std::ofstream f(g_self_heartbeat_path, std::ios::trunc);
        if (f) f << os.str();
    }
}

static bool parse_kv(const std::string& line, std::string* k, std::string* v) {
    auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    *k = line.substr(0, eq);
    *v = line.substr(eq + 1);
    while (!v->empty() && (v->back() == '\r' || v->back() == '\n')) v->pop_back();
    return true;
}

static bool read_instance(const fs::path& path, s_instance_info* out) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line, k, v;
    while (std::getline(f, line)) {
        if (!parse_kv(line, &k, &v)) continue;
        if      (k == "uuid")                out->uuid = v;
        else if (k == "pid")                 out->pid = (uint32_t)atoi(v.c_str());
        else if (k == "bind_port")           out->bind_port = atoi(v.c_str());
        else if (k == "peer_addr")           out->peer_addr = v;
        else if (k == "peer_port")           out->peer_port = atoi(v.c_str());
        else if (k == "hostname")            out->hostname = v;
        else if (k == "label")               out->label = v;
        else if (k == "timestamp_ms")        out->last_seen_ms = (uint64_t)strtoull(v.c_str(), nullptr, 10);
        else if (k == "in_game")             out->in_game = (atoi(v.c_str()) != 0);
        else if (k == "module")              out->module = atoi(v.c_str());
        else if (k == "mode")                out->mode = atoi(v.c_str());
        else if (k == "map_id")              out->map_id = atoi(v.c_str());
        else if (k == "hopper_game_variant") out->hopper_game_variant = atoi(v.c_str());
        else if (k == "hopper_map_variant")  out->hopper_map_variant  = atoi(v.c_str());
        else if (k == "difficulty")          out->difficulty = atoi(v.c_str());
        else if (k == "map_name")            out->map_name = v;
        else if (k == "session_identity")    out->session_identity = strtoull(v.c_str(), nullptr, 10);
    }
    return !out->uuid.empty();
}

static void scan_directory() {
    std::vector<s_instance_info> next;
    std::error_code ec;
    if (!fs::exists(g_discovery_dir, ec)) return;

    uint64_t now = now_ms();
    uint64_t cutoff = (now > 6000) ? (now - 6000) : 0;

    for (auto& entry : fs::directory_iterator(g_discovery_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".halox") continue;
        s_instance_info info;
        if (!read_instance(entry.path(), &info)) continue;
        if (info.uuid == g_self_uuid) continue;
        if (info.last_seen_ms < cutoff) {
            // Stale entry — remove the file (process probably crashed).
            std::error_code ec2;
            fs::remove(entry.path(), ec2);
            continue;
        }
        next.push_back(std::move(info));
    }

    std::lock_guard<std::mutex> lk(g_list_mutex);
    g_list = std::move(next);
}

static DWORD WINAPI heartbeat_thread_proc(LPVOID) {
    while (!g_stop.load()) {
        write_heartbeat();
        scan_directory();
        // 2-second cadence with refresh-kick fast-wakeup.
        for (int i = 0; i < 20; ++i) {
            if (g_stop.load()) break;
            if (g_refresh_kick.exchange(false)) break;
            Sleep(100);
        }
    }
    std::error_code ec;
    fs::remove(g_self_heartbeat_path, ec);
    return 0;
}

void instance_discovery_init() {
    bool expected = false;
    if (!g_inited.compare_exchange_strong(expected, true)) return;

    g_self_uuid     = make_uuid();
    g_self_label    = read_env_str("HALOX_INSTANCE", "");
    g_self_pid      = (uint32_t)GetCurrentProcessId();
    g_self_hostname = get_hostname();
    g_self_bind_port= read_env_int("HALOX_BIND_PORT", 0);
    g_self_peer_port= read_env_int("HALOX_PEER_PORT", 0);
    g_self_peer_addr= read_env_str("HALOX_PEER_ADDR", "");

    // Per-process random 64-bit session identity. Goes into the heartbeat so a
    // joining peer knows what to stamp into Reach's join descriptor at +0x58.
    // Real MCC builds these from network credentials; we fake one. As long as
    // both peers see the same value, the engine state machine is happy.
    {
        uint64_t hi = (uint64_t)GetTickCount64();
        uint64_t lo = ((uint64_t)g_self_pid << 16) ^ ((uint64_t)g_self_bind_port);
        g_self_session_identity = (hi << 32) ^ lo ^ 0xA51DD80529'42D301ULL;
        if (g_self_session_identity == 0) g_self_session_identity = 1;
    }

    g_discovery_dir = discovery_directory();
    char fname[128];
    snprintf(fname, sizeof(fname), "%s.halox", g_self_uuid.c_str());
    g_self_heartbeat_path = g_discovery_dir / fname;

    CONSOLE_LOG_INFO("instance_discovery: uuid=%s label=%s pid=%u bind=%d dir=%s",
        g_self_uuid.c_str(),
        g_self_label.empty() ? "<unset>" : g_self_label.c_str(),
        g_self_pid, g_self_bind_port,
        g_discovery_dir.string().c_str());

    HANDLE h = CreateThread(nullptr, 0, &heartbeat_thread_proc, nullptr, 0, nullptr);
    if (!h) {
        CONSOLE_LOG_WARN("instance_discovery: CreateThread failed (gle=%lu)", GetLastError());
        g_inited = false;
        return;
    }
    SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
    CloseHandle(h);
}

void instance_discovery_set_session(
    int module, int mode, int map_id,
    int hopper_game_variant, int hopper_map_variant, int difficulty,
    const char* map_name)
{
    std::lock_guard<std::mutex> lk(g_session_mutex);
    g_session_module = module;
    g_session_mode = mode;
    g_session_map_id = map_id;
    g_session_hopper_game_variant = hopper_game_variant;
    g_session_hopper_map_variant = hopper_map_variant;
    g_session_difficulty = difficulty;
    g_session_map_name = map_name ? map_name : "";
    g_session_in_game = (map_id >= 0 && module >= 0);
    g_refresh_kick.store(true);
}

std::vector<s_instance_info> instance_discovery_list() {
    std::lock_guard<std::mutex> lk(g_list_mutex);
    return g_list;
}

void instance_discovery_refresh() {
    g_refresh_kick.store(true);
}

const char* instance_discovery_self_uuid() {
    return g_self_uuid.c_str();
}

const char* instance_discovery_self_label() {
    return g_self_label.c_str();
}

int instance_discovery_self_bind_port() {
    return g_self_bind_port;
}

} // namespace halox::network
