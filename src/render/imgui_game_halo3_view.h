#pragma once

#include "imgui_view.h"

class c_imgui_game_halo3_view : public c_imgui_view {
public:
	void render() override;
private:
	void render_debug();
	void render_engine();
	void render_observer();
	void render_director();
};
