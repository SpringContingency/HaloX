#pragma once

#include "imgui_view.h"

class c_imgui_game_view : public c_imgui_view {
public:
	void render() override;
protected:
	virtual void render_internal() = 0;
};

class c_imgui_game_mainmenu_view : public c_imgui_game_view {
private:
	void render_internal() override;
	void render_campaign();
	void render_multiplayer();
};

class c_imgui_game_ingame_view : public c_imgui_game_view {
private:
	void render_internal() override;
};
