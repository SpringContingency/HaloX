#include "render_player_view.h"

#include "../../halo3.h"
#include "../../cache/tag_cache.h"

using namespace libmcc;
using namespace libmcc::halo3;

void ::c_player_view::render() {
	render_dollycam();
	render_debug_structure();
}

int ::c_player_view::render_setup() {
	return 0;
}

int :: c_player_view::compute_visibility() {
	return 0;
}

int ::c_player_view::render_submit_visibility() {
	return 0;
}

void ::c_player_view::render_debug_structure() {
	if (!g_halo3_parameters.debug_structure && !g_halo3_parameters.debug_instanced_geometry) {
		return;
	}

	auto tls = g_thread_local_storage();

	if (!game_in_progress()) {
		return;
	}

	auto scnr = global_scenario();

	if (scnr == nullptr) {
		return;
	}

	s_tag_cache tag_cache;
	c_render_debug_line_drawer drawer;

	if (g_halo3_parameters.debug_structure) {
		drawer.set_color(&g_halo3_parameters.debug_structure_color);

		auto bsp_index = 0;

		auto active_bsp_mask = tls->game_globals->active_structure_bsp_mask;

		for (auto& structure_bsp_block : tag_cache.get_tag_block(scnr->structure_bsps)) {
			if ((active_bsp_mask & (1 << bsp_index++)) == 0) {
				continue;
			}

			auto structure_bsp = tag_cache.get_tag_reference(structure_bsp_block.structure_bsp);

			if (structure_bsp == nullptr) {
				continue;
			}

			auto structure_bsp_resources = structure_bsp->resource_interface.get_resources();

			for (auto& collision_bsp : tag_cache.get_tag_block(structure_bsp_resources->collision_bsp)) {
				auto vertices = tag_cache.get_tag_block(collision_bsp.vertices).begin();

				for (auto& edge : tag_cache.get_tag_block(collision_bsp.edges)) {
					auto v0 = &vertices[edge.vertex_indices[0]].point;
					auto v1 = &vertices[edge.vertex_indices[1]].point;

					drawer.add_line_3d(v0, v1);
				}
			}
		}

		drawer.flush();
	}

	if (g_halo3_parameters.debug_instanced_geometry) {
		drawer.set_color(&g_halo3_parameters.debug_instanced_geometry_color);

		for (auto& visible_instance : c_visible_items::m_item()->instances) {
			auto instance_index = visible_instance.instanced_geometry_instances_index;

			auto bsp_index = visible_instance.structure_bsp_index;

			auto bsp = tag_cache.get_tag_reference((tag_cache.get_tag_block(scnr->structure_bsps).begin() + bsp_index)->structure_bsp);

			auto instance = tag_cache.get_tag_block(bsp->instanced_geometry_instances).begin() + instance_index;

			auto instance_matrix = reinterpret_cast<const real_matrix4x3*>(&instance->scale);

			auto instance_definition_index = instance->instance_definition;

			if (instance_definition_index == -1) {
				continue;
			}

			auto bsp_resource = bsp->resource_interface.get_resources();

			auto instance_definition = tag_cache.get_tag_block(bsp_resource->instanced_geometries_definitions).begin() + instance_definition_index;

			auto vertices = tag_cache.get_tag_block(instance_definition->vertices).begin();

			for (auto& edge : tag_cache.get_tag_block(instance_definition->edges)) {
				auto v0 = instance_matrix->transform(vertices[edge.vertex_indices[0]].point);
				auto v1 = instance_matrix->transform(vertices[edge.vertex_indices[1]].point);

				drawer.add_line_3d(&v0, &v1);
			}
		}

		drawer.flush();
	}
}

void ::c_player_view::render_dollycam() {
	/*if (!m_visibility) {
		return;
	}

	if (m_model == nullptr) {
		return;
	}

	std::priority_queue<float> ticks;
	std::list<s_keyframe>::const_iterator it[3];
	std::list<s_keyframe>::const_iterator next[3];
	std::list<s_keyframe>::const_iterator end[3];

	for (int i = 0; i <= _dollycam_series_position_z; ++i) {
		auto keyframes = &m_model->series().at(i).keyframes;

		keyframes->begin();

		if (keyframes->size() <= 1) {
			return;
		}

		for (auto& keyframe : *keyframes) {
			ticks.push(keyframe.data.x);
		}

		it[i] = keyframes->begin();
		next[i] = it[i]; ++next[i];
		end[i] = keyframes->end();
	}

	float tick_begin = ticks.top(); ticks.pop();
	float tick_end = ticks.top(); ticks.pop();

	real_point3d positions[2];

	while (!ticks.empty()) {
		int p0 = -1;
		int p1 = 0;
		c_render_debug_line_drawer drawer;

		for (int l = 0; l <= m_level; ++l) {
			auto tick = tick_begin + (tick_end - tick_begin) * l / m_level;

			for (int i = 0; i <= _dollycam_series_position_z; ++i) {
				if (next[i] != end[i] && next[i]->data.x < tick_end) {
					++it[i];
					++next[i];
				}

				if (next[i] != end[i] && it[i]->data.x < tick_end) {
					positions[p1].n[i] = it[i]->calculate(tick, &*next[i]);
				} else {
					positions[p1].n[i] = it[i]->data.y;
				}

				if (p0 != -1) {
					drawer.add_line_3d(positions + p0, positions + p1);
				}

				p0 = p1;
				p1 = (p1 + 1) % 2;
			}
		}

		tick_begin = tick_end;
		tick_end = ticks.top();
		ticks.pop();
	}*/
}
