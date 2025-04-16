#pragma once

#include <libmcc/libmcc.h>

struct c_player_view : libmcc::halo3::c_player_view {
    void render() override;
    int render_setup() override;
    int compute_visibility() override;
    int render_submit_visibility() override;

	void render_debug_structure();
    void render_dollycam();
};
