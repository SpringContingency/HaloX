#include "main.h"

#include <imgui.h>

using namespace libmcc;
using namespace halo3;

static void render_debug(s_thread_local_storage* tls, bool* show);
static void render_engine(s_thread_local_storage* tls, bool* show);
static void render_director(s_thread_local_storage* tls, bool* show);
static void render_observer(s_thread_local_storage* tls, bool* show);
static void render_dollycam(s_thread_local_storage* tls, bool* show);

void main_render_game_halo3() {
    static bool show_engine = false;
    static bool show_director = false;
    static bool show_observer = false;
    static bool show_dollycam = false;
    static bool show_debug = false;

    auto tls = g_thread_local_storage();

    if (ImGui::BeginMainMenuBar()) {
        ImGui::MenuItem("Debug", nullptr, &show_debug);
        ImGui::MenuItem("Engine", nullptr, &show_engine);
        ImGui::MenuItem("Director", nullptr, &show_director);
        ImGui::MenuItem("Observer", nullptr, &show_observer);

        ImGui::EndMainMenuBar();
    }

    if (show_debug) {
        render_debug(tls, &show_debug);
    }

    if (show_engine) {
        render_engine(tls, &show_engine);
    }

    if (show_director) {
        render_director(tls, &show_director);
    }

    if (show_observer) {
        render_observer(tls, &show_observer);
    }
}

static void render_debug(s_thread_local_storage* tls, bool* show) {
    do {
        if (!ImGui::Begin("Debug", show)) {
            break;
        }

    } while (false);
    ImGui::End();
}

static void render_engine(s_thread_local_storage* tls, bool* show) {
    do {
        if (!ImGui::Begin("Engine", show)) {
            break;
        }

        auto engine = tls->game_engine_globals;

        auto map_variant = &engine->map_variant;

        ImGui::Text("Number Of Variant Objects: %d", map_variant->m_number_of_variant_objects);

    } while (false);
    ImGui::End();
}

static void render_director(s_thread_local_storage* tls, bool* show) {
    do {
        if (!ImGui::Begin("Director", show)) {
            break;
        }

        auto director = tls->director_globals;

        auto info = &director->infos[0];

        auto camera = &director->directors[0].director.m_camera.camera;

        ImGui::Text("Camera Type: %d", info->camera_mode);

        switch (info->camera_mode) {
        case _camera_mode_flying: {
            auto flying_camera = reinterpret_cast<c_flying_camera*>(camera);
            ImGui::Text("Position: %f %f %f",
                flying_camera->m_position.x, flying_camera->m_position.y, flying_camera->m_position.z);

            ImGui::Text("Facing: %f %f",
                flying_camera->m_facing.yaw * RAD_TO_DEG, flying_camera->m_facing.pitch * RAD_TO_DEG);

            ImGui::Text("Roll: %f", flying_camera->m_roll * RAD_TO_DEG);
            break;
        }
        default:
            break;
        }

    } while (false);

    ImGui::End();
}

static void render_observer(s_thread_local_storage* tls, bool* show) {
    do {
        if (!ImGui::Begin("Observer", show)) {
            break;
        }
        auto observer = tls->g_observer_globals;

        auto position = &observer->observers[0].result.focus_point;

        real_euler_angles2d angles(observer->observers[0].result.forward);

        real_euler_angles2d up(observer->observers[0].result.up);

        ImGui::Text("%.3f %.3f %.3f / %.2f %.2f / %.2f %.2f",
            position->x, position->y, position->z,
            angles.yaw * RAD_TO_DEG, angles.pitch * RAD_TO_DEG,
            up.yaw * RAD_TO_DEG, up.pitch * RAD_TO_DEG);
    } while (false);

    ImGui::End();
}

static void render_dollycam(s_thread_local_storage* tls, bool* show) {
    do {
        if (!ImGui::Begin("Property Editor", show)) {
            break;
        }

        auto director_globals = tls->director_globals;
        auto observer_globals = tls->g_observer_globals;
        auto gametime_globals = tls->game_time_globals;

        auto info = &director_globals->infos[0];

        auto camera = &director_globals->directors[0].director.m_camera.camera;

        if (info->camera_mode != _camera_mode_flying) {
            ImGui::Text("Please Change Camera Mode To Flying Mode!");
            break;
        }

        auto observer = observer_globals->observers + 0;
        auto flying_camera = reinterpret_cast<c_flying_camera*>(camera);
    } while (false);

    ImGui::End();
}
