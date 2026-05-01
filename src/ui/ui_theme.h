#pragma once

#include <imgui.h>

// Halo classic-modern hybrid theme. Cyan = passive chrome (borders, inactive
// labels, separators). Amber = active focus / hover / call-to-action. Hard
// edges, no rounded corners, generous padding.
//
// Apply once at startup AFTER ImGui context creation:
//   ui_theme_apply();
//
// Designed so that a future custom-rendered backend (Option B) can read the
// same color/sizing constants — keeping the visual language consistent across
// imgui and any retained-mode replacement.

namespace halox::ui {

// Palette ---------------------------------------------------------------------
// Stored as ImVec4 (rgba 0..1) for direct imgui use.

extern const ImVec4 col_bg;             // primary window/panel fill (#06080A)
extern const ImVec4 col_bg_alt;         // alt/inset panel fill
extern const ImVec4 col_border;         // thin cyan rule (#1FCECB @ alpha)
extern const ImVec4 col_border_strong;  // brighter cyan
extern const ImVec4 col_text;           // primary text (off-white)
extern const ImVec4 col_text_dim;       // secondary text
extern const ImVec4 col_text_disabled;  // ghosted text
extern const ImVec4 col_accent;         // amber focus (#FFB347)
extern const ImVec4 col_accent_hi;      // hover amber (#FFC966)
extern const ImVec4 col_accent_dim;     // muted amber for inactive highlights
extern const ImVec4 col_warning;        // warning/error text
extern const ImVec4 col_scanline;       // scanline overlay tint

// Sizing ----------------------------------------------------------------------

constexpr float k_border_thin   = 1.0f;
constexpr float k_border_strong = 2.0f;
constexpr float k_bracket_len   = 14.0f;
constexpr float k_pad_window    = 14.0f;
constexpr float k_pad_frame_x   = 10.0f;
constexpr float k_pad_frame_y   = 6.0f;

// Apply the global ImGuiStyle (colors + sizing). Idempotent.
void ui_theme_apply();

}  // namespace halox::ui
