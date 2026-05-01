#include "imgui_session_browser_view.h"

#include "../game/game_instance_manager.h"
#include "../game/map_names.h"
#include "../game/mcc_user_settings.h"
#include "../network/instance_discovery.h"
#include "../network/mp_session_inject.h"
#include "../logging/logging.h"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>

using namespace halox::network;
using namespace libmcc;

static const char* k_module_names_short[] = {
    "halo1", "halo2", "halo3", "halo4", "groundhog", "halo3odst", "haloreach"
};
static const char* k_mode_names_short[] = {
    "", "campaign", "spartan_ops", "multiplayer", "ui_shell", "firefight"
};

static const char* mod_label(int m) {
    if (m < 0 || m >= (int)(sizeof(k_module_names_short) / sizeof(*k_module_names_short))) return "?";
    return k_module_names_short[m];
}
static const char* mode_label(int m) {
    if (m < 0 || m >= (int)(sizeof(k_mode_names_short) / sizeof(*k_mode_names_short))) return "?";
    return k_mode_names_short[m];
}

bool c_imgui_session_browser_view::begin(bool* show) {
    ImGui::SetNextWindowSize(ImVec2(720.0f, 360.0f), ImGuiCond_FirstUseEver);
    return ImGui::Begin("Session Browser", show);
}

void c_imgui_session_browser_view::end() {
    ImGui::End();
}

void c_imgui_session_browser_view::render() {
    // Self badge.
    {
        const char* lbl = instance_discovery_self_label();
        ImGui::TextDisabled("This instance: uuid=%.8s%s%s | bind_port=%d",
            instance_discovery_self_uuid(),
            (lbl && *lbl) ? " | label=" : "",
            (lbl && *lbl) ? lbl : "",
            instance_discovery_self_bind_port());
    }

    if (ImGui::Button("Refresh")) {
        instance_discovery_refresh();
    }
    ImGui::SameLine();

    auto list = instance_discovery_list();
    ImGui::TextDisabled("(%d session%s discovered)",
        (int)list.size(), list.size() == 1 ? "" : "s");

    ImGui::Separator();

    bool haloreach_loaded = (GetModuleHandleA("haloreach.dll") != nullptr);
    if (game_instance_manager()->in_game()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "In-game — Join uses Reach's join-state machine to leave the current session and join the host.");
    } else if (haloreach_loaded) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "Engine ready — Join hands a descriptor to Reach's session-join state machine.");
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f),
            "Click Join to launch Reach with the host's map and inject the join descriptor in one shot.");
    }
    ImGui::Separator();

    if (list.empty()) {
        ImGui::TextDisabled("No other halox instances are advertising.");
        ImGui::TextDisabled("Launch another halox process and pick a Map/Mode there to host.");
        return;
    }

    if (ImGui::BeginTable("sessions", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Host",   ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Title",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Mode",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Map",    ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableHeadersRow();

        for (auto& s : list) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (!s.label.empty()) {
                ImGui::Text("%s [%s]:%d", s.hostname.c_str(), s.label.c_str(), s.bind_port);
            } else {
                ImGui::Text("%s:%d", s.hostname.c_str(), s.bind_port);
            }
            ImGui::TextDisabled("uuid=%.8s pid=%u", s.uuid.c_str(), s.pid);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mod_label(s.module));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(mode_label(s.mode));

            ImGui::TableNextColumn();
            if (!s.map_name.empty()) {
                ImGui::TextUnformatted(s.map_name.c_str());
            } else if (s.map_id >= 0) {
                ImGui::Text("map_id=%d", s.map_id);
            } else {
                ImGui::TextDisabled("(no map)");
            }

            ImGui::TableNextColumn();
            if (s.in_game) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "in-game");
            } else {
                ImGui::TextDisabled("idle");
            }

            ImGui::TableNextColumn();
            char btn_id[64];
            snprintf(btn_id, sizeof(btn_id), "Join##%s", s.uuid.c_str());

            // Join always renders enabled. If the host's heartbeat is missing
            // fields we still log the click and the reason so we can diagnose
            // silent-button cases instead of guessing why nothing happened.
            const char* missing = nullptr;
            if (!s.in_game)        missing = "host not in_game";
            else if (s.module < 0) missing = "module<0";
            else if (s.mode   < 0) missing = "mode<0";
            else if (s.map_id < 0) missing = "map_id<0";
            if (missing) {
                ImGui::TextDisabled("(%s)", missing);
                ImGui::SameLine();
            }
            if (ImGui::Button(btn_id)) {
                if (missing) {
                    CONSOLE_LOG_WARN("session_browser: Join clicked but bailed — %s "
                        "(uuid=%.8s in_game=%d module=%d mode=%d map_id=%d)",
                        missing, s.uuid.c_str(),
                        (int)s.in_game, s.module, s.mode, s.map_id);
                    continue;
                }
                const char* peer_addr = s.peer_addr.empty() ? "127.0.0.1" : s.peer_addr.c_str();
                char port_buf[16], sid_buf[32], map_buf[16];
                snprintf(port_buf, sizeof(port_buf), "%d", s.bind_port);
                snprintf(sid_buf,  sizeof(sid_buf),  "%llu",
                    (unsigned long long)s.session_identity);
                snprintf(map_buf,  sizeof(map_buf),  "%d", s.map_id);
                SetEnvironmentVariableA("HALOX_PEER_ADDR",       peer_addr);
                SetEnvironmentVariableA("HALOX_PEER_PORT",       port_buf);
                SetEnvironmentVariableA("HALOX_PEER_SESSION_ID", sid_buf);
                SetEnvironmentVariableA("HALOX_PEER_MAP_ID",     map_buf);
                SetEnvironmentVariableA("HALOX_MP_INJECT_REAL",  "1");
                SetEnvironmentVariableA("HALOX_MP_INJECT",       "");

                CONSOLE_LOG_INFO("session_browser: joining %s @ %s:%d (module=%d mode=%d map=%d \"%s\") haloreach_loaded=%d in_game=%d",
                    s.hostname.c_str(), peer_addr, s.bind_port,
                    s.module, s.mode, s.map_id, s.map_name.c_str(),
                    (int)haloreach_loaded,
                    (int)game_instance_manager()->in_game());

                if (haloreach_loaded) {
                    // Engine already running (we're either at the menu in
                    // haloreach or already in a Reach session). Apply the
                    // inject in-place; Reach's own join state machine handles
                    // tearing down any current session before joining.
                    uint64_t peer_guid = ((uint64_t)s.bind_port << 32)
                                       | (uint64_t)(uint32_t)std::strtoul(s.uuid.c_str(), nullptr, 16);
                    if (peer_guid == 0) peer_guid = 0xCAFEBABEDEADBEEFULL;
                    halox::network::mp_session_inject_real_apply(peer_guid, /*is_host=*/false);
                } else {
                    // haloreach.dll isn't loaded yet — user clicked Join from
                    // the halox menu without entering Reach first. Kick the
                    // launch_game path with the host's prop. initialize_game
                    // loads haloreach, sees HALOX_MP_INJECT_REAL=1, and runs
                    // mp_session_inject_real_apply once the engine is up.
                    s_game_prop prop{};
                    prop.module = (e_module)s.module;
                    prop.mode   = (e_game_mode)s.mode;
                    prop.difficulty = (e_campaign_difficulty_level)
                        (s.difficulty >= 0 ? s.difficulty : (int)_campaign_difficulty_level_normal);
                    prop.map.builtin_map_id = (e_map_id)s.map_id;
                    prop.map.flags = 0x8888;
                    prop.hopper_game_variant = s.hopper_game_variant;
                    prop.hopper_map_variant  = s.hopper_map_variant;
                    apply_mcc_settings_for_module(prop.module);
                    int rc = game_instance_manager()->launch_game(&prop);
                    if (rc != 0) {
                        CONSOLE_LOG_WARN("session_browser: launch_game returned %d", rc);
                    }
                }

                instance_discovery_set_session(
                    s.module, s.mode, s.map_id,
                    s.hopper_game_variant, s.hopper_map_variant,
                    s.difficulty, s.map_name.c_str());
            }
        }

        ImGui::EndTable();
    }
}
