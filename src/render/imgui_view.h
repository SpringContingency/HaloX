#pragma once

#include <imgui.h>
#include <imgui_internal.h>

class i_imgui_view {
public:
	virtual bool begin(bool* open = nullptr) = 0;
	virtual void end() = 0;
};
