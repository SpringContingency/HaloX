#pragma once

#include "imgui_view.h"

class c_imgui_player_view : public c_imgui_view {
public:
	bool begin(bool* show) override;
	void render() override;
	void end() override;
private:
	void render_info();
	void render_customization();
	void render_setting();
};
