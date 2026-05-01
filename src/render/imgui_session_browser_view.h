#pragma once

#include "imgui_view.h"

class c_imgui_session_browser_view : public c_imgui_view {
public:
    bool begin(bool* show = nullptr) override;
    void render() override;
    void end() override;
};
