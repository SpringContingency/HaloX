#include "ui_theme.h"

namespace halox::ui {

// Palette ---------------------------------------------------------------------
const ImVec4 col_bg            = ImVec4(0.024f, 0.031f, 0.039f, 1.00f);  // #06080A
const ImVec4 col_bg_alt        = ImVec4(0.039f, 0.051f, 0.063f, 1.00f);  // #0A0D10
const ImVec4 col_border        = ImVec4(0.122f, 0.808f, 0.796f, 0.55f);  // #1FCECB @55%
const ImVec4 col_border_strong = ImVec4(0.122f, 0.808f, 0.796f, 0.95f);  // #1FCECB @95%
const ImVec4 col_text          = ImVec4(0.902f, 0.945f, 0.949f, 1.00f);  // #E6F1F2
const ImVec4 col_text_dim      = ImVec4(0.478f, 0.541f, 0.549f, 1.00f);  // #7A8A8C
const ImVec4 col_text_disabled = ImVec4(0.282f, 0.318f, 0.325f, 1.00f);  // #485153
const ImVec4 col_accent        = ImVec4(1.000f, 0.702f, 0.278f, 1.00f);  // #FFB347
const ImVec4 col_accent_hi     = ImVec4(1.000f, 0.788f, 0.400f, 1.00f);  // #FFC966
const ImVec4 col_accent_dim    = ImVec4(0.502f, 0.349f, 0.137f, 0.85f);  // #80592C
const ImVec4 col_warning       = ImVec4(1.000f, 0.345f, 0.235f, 1.00f);  // #FF583C
const ImVec4 col_scanline      = ImVec4(0.122f, 0.808f, 0.796f, 0.05f);  // cyan @5%

void ui_theme_apply() {
	ImGuiStyle& s = ImGui::GetStyle();

	// Sharp, blocky chrome — Halo-CE-derived. No rounding anywhere.
	s.WindowRounding    = 0.0f;
	s.ChildRounding     = 0.0f;
	s.FrameRounding     = 0.0f;
	s.GrabRounding      = 0.0f;
	s.PopupRounding     = 0.0f;
	s.ScrollbarRounding = 0.0f;
	s.TabRounding       = 0.0f;

	// Clean geometry.
	s.WindowBorderSize = k_border_thin;
	s.ChildBorderSize  = k_border_thin;
	s.FrameBorderSize  = k_border_thin;
	s.PopupBorderSize  = k_border_thin;
	s.TabBorderSize    = 0.0f;

	// Generous spacing — modern Halo lets things breathe.
	s.WindowPadding   = ImVec2(k_pad_window, k_pad_window);
	s.FramePadding    = ImVec2(k_pad_frame_x, k_pad_frame_y);
	s.ItemSpacing     = ImVec2(10.0f, 8.0f);
	s.ItemInnerSpacing= ImVec2(6.0f, 6.0f);
	s.IndentSpacing   = 18.0f;
	s.ScrollbarSize   = 12.0f;
	s.GrabMinSize     = 14.0f;

	// Wide-tracked headers via slight default font scale on display elements
	// would require a separate font; skipped this iteration to avoid pulling
	// a TTF into the repo. Headers still feel distinct via color alone.

	auto& C = s.Colors;
	C[ImGuiCol_WindowBg]              = col_bg;
	C[ImGuiCol_ChildBg]               = col_bg;
	C[ImGuiCol_PopupBg]               = col_bg_alt;
	C[ImGuiCol_MenuBarBg]             = col_bg_alt;
	C[ImGuiCol_Border]                = col_border;
	C[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

	C[ImGuiCol_Text]                  = col_text;
	C[ImGuiCol_TextDisabled]          = col_text_disabled;

	// Frame backgrounds (input fields, combos, sliders).
	C[ImGuiCol_FrameBg]               = ImVec4(col_bg_alt.x, col_bg_alt.y, col_bg_alt.z, 0.85f);
	C[ImGuiCol_FrameBgHovered]        = ImVec4(col_accent_dim.x, col_accent_dim.y, col_accent_dim.z, 0.40f);
	C[ImGuiCol_FrameBgActive]         = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.35f);

	// Title bars.
	C[ImGuiCol_TitleBg]               = col_bg_alt;
	C[ImGuiCol_TitleBgActive]         = col_bg_alt;
	C[ImGuiCol_TitleBgCollapsed]      = ImVec4(col_bg_alt.x, col_bg_alt.y, col_bg_alt.z, 0.65f);

	// Scrollbars.
	C[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
	C[ImGuiCol_ScrollbarGrab]         = col_border;
	C[ImGuiCol_ScrollbarGrabHovered]  = col_accent_dim;
	C[ImGuiCol_ScrollbarGrabActive]   = col_accent;

	// Interactive accents — amber on focus, cyan on idle.
	C[ImGuiCol_CheckMark]             = col_accent;
	C[ImGuiCol_SliderGrab]            = col_accent;
	C[ImGuiCol_SliderGrabActive]      = col_accent_hi;

	C[ImGuiCol_Button]                = ImVec4(col_bg_alt.x, col_bg_alt.y, col_bg_alt.z, 0.85f);
	C[ImGuiCol_ButtonHovered]         = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.30f);
	C[ImGuiCol_ButtonActive]          = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.55f);

	C[ImGuiCol_Header]                = ImVec4(col_accent_dim.x, col_accent_dim.y, col_accent_dim.z, 0.50f);
	C[ImGuiCol_HeaderHovered]         = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.40f);
	C[ImGuiCol_HeaderActive]          = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.65f);

	C[ImGuiCol_Separator]             = col_border;
	C[ImGuiCol_SeparatorHovered]      = col_accent;
	C[ImGuiCol_SeparatorActive]       = col_accent_hi;

	C[ImGuiCol_ResizeGrip]            = ImVec4(col_border.x, col_border.y, col_border.z, 0.35f);
	C[ImGuiCol_ResizeGripHovered]     = col_accent;
	C[ImGuiCol_ResizeGripActive]      = col_accent_hi;

	C[ImGuiCol_Tab]                   = col_bg_alt;
	C[ImGuiCol_TabHovered]            = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.40f);
	C[ImGuiCol_TabSelected]           = ImVec4(col_accent_dim.x, col_accent_dim.y, col_accent_dim.z, 0.85f);
	C[ImGuiCol_TabDimmed]             = col_bg_alt;
	C[ImGuiCol_TabDimmedSelected]     = col_bg_alt;

	C[ImGuiCol_PlotLines]             = col_border_strong;
	C[ImGuiCol_PlotLinesHovered]      = col_accent;
	C[ImGuiCol_PlotHistogram]         = col_accent;
	C[ImGuiCol_PlotHistogramHovered]  = col_accent_hi;

	C[ImGuiCol_TableHeaderBg]         = col_bg_alt;
	C[ImGuiCol_TableBorderStrong]     = col_border_strong;
	C[ImGuiCol_TableBorderLight]      = col_border;
	C[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
	C[ImGuiCol_TableRowBgAlt]         = ImVec4(col_border.x, col_border.y, col_border.z, 0.04f);

	C[ImGuiCol_TextSelectedBg]        = ImVec4(col_accent.x, col_accent.y, col_accent.z, 0.40f);

	C[ImGuiCol_NavCursor]             = col_accent;
	C[ImGuiCol_NavWindowingHighlight] = col_accent;
	C[ImGuiCol_NavWindowingDimBg]     = ImVec4(col_bg.x, col_bg.y, col_bg.z, 0.65f);
	C[ImGuiCol_ModalWindowDimBg]      = ImVec4(col_bg.x, col_bg.y, col_bg.z, 0.75f);
}

}  // namespace halox::ui
