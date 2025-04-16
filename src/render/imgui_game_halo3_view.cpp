#include "imgui_game_halo3_view.h"

#include <libmcc/libmcc.h>

using namespace libmcc;
using namespace halo3;

static bool show_debug;
static bool show_engine;
static bool show_observer;
static bool show_director;
static libmcc::halo3::s_thread_local_storage* tls;

void c_imgui_game_halo3_view::render() {
    if (ImGui::BeginMainMenuBar()) {
        ImGui::MenuItem("Debug", nullptr, &show_debug);
        ImGui::MenuItem("Engine", nullptr, &show_engine);
        ImGui::MenuItem("Director", nullptr, &show_director);
        ImGui::MenuItem("Observer", nullptr, &show_observer);

        ImGui::EndMainMenuBar();
    }

    tls = g_thread_local_storage(); // update tls pointer cache

    if (show_debug) {
        render_debug();
    }

    if (show_engine) {
        render_engine();
    }

    if (show_director) {
        render_director();
    }

    if (show_observer) {
        render_observer();
    }
}

void c_imgui_game_halo3_view::render_debug() {
    do {
        if (!ImGui::Begin("Debug", &show_debug)) {
            break;
        }

    } while (false);
    ImGui::End();
}

void c_imgui_game_halo3_view::render_engine() {
    do {
        if (!ImGui::Begin("Engine", &show_engine)) {
            break;
        }

        auto engine = tls->game_engine_globals;

        auto map_variant = &engine->map_variant;

        ImGui::Text("Number Of Variant Objects: %d", 
            map_variant->m_number_of_variant_objects);

    } while (false);
    ImGui::End();
}

void c_imgui_game_halo3_view::render_observer() {
    do {
        if (!ImGui::Begin("Observer", &show_observer)) {
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

void c_imgui_game_halo3_view::render_director() {
    do {
        if (!ImGui::Begin("Director", &show_director)) {
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
