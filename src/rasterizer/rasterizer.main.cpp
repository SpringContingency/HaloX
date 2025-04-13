#include "rasterizer.h"

c_rasterizer c_rasterizer::g_rasterizer;

int c_rasterizer::initialize() {
	if (initialize_window()) {
		return 1;
	}

	if (initialize_device()) {
		shutdown_window();
		return 2;
	}

	if (initialize_imgui()) {
		shutdown_device();
		shutdown_window();
		return 3;
	}

	initialize_surface();
	initialize_shader();

	return 0;
}

int c_rasterizer::shutdown() {
	shutdown_imgui();
	shutdown_device();
	shutdown_window();
	destory_surface();
	destory_shader();
	return 0;
}

int c_rasterizer::begin_frame() {
	begin_frame_device();
	begin_frame_imgui();
	return 0;
}

int c_rasterizer::end_frame() {
	end_frame_imgui();
	end_frame_device();
	return 0;
}

int c_rasterizer::resize(int width, int height) {
	destory_surface();
	resize_device(width, height);
	initialize_surface();
	return 0;
}
