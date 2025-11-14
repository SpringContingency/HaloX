#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

class c_imgui_view {
public:
	virtual bool begin(bool* show = nullptr) {return true;};
	virtual void render() {};
	virtual void end() {}
};
