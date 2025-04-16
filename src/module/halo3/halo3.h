#pragma once

#include <libmcc/libmcc.h>

extern struct s_halo3_parameters {
	bool debug_structure;
	libmcc::real_rgba_color debug_structure_color = libmcc::g_real_rgba_white;
	bool debug_instanced_geometry;
	libmcc::real_rgba_color debug_instanced_geometry_color = libmcc::g_real_rgba_blue;
} g_halo3_parameters;