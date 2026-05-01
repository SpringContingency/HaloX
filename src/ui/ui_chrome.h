#pragma once

#include <imgui.h>

// Halo-themed drawing primitives. All operate on an ImDrawList — pass either
// the foreground draw list (for full-window overlays) or a window-local draw
// list (for per-widget chrome).

namespace halox::ui {

// Four corner brackets framing a rectangle (Halo CE / Infinite reticle vibe).
// `len` controls the leg length; `thickness` the line width. Color defaults
// to col_accent.
void chrome_corner_brackets(
    ImDrawList* dl,
    ImVec2 min, ImVec2 max,
    float len = 14.0f,
    float thickness = 2.0f,
    ImU32 color = 0);

// Scanline overlay across the full given rect. Cyan tint @ ~5% alpha, every
// 3 pixels. Adds film/sci-fi feel without obscuring content.
void chrome_scanlines(
    ImDrawList* dl,
    ImVec2 min, ImVec2 max,
    int spacing = 3);

// Soft radial vignette darkening the corners of the given rect. Cheap fake:
// four edge gradients composited.
void chrome_vignette(
    ImDrawList* dl,
    ImVec2 min, ImVec2 max,
    float strength = 0.40f);

// Section header: amber notch + uppercase text.
//   ▮ TITLE
// Anchored to current cursor X, advances cursor Y by line height + padding.
void chrome_section_header(const char* label);

// Animated loading ring. `t_seconds` is monotonic time used to animate the
// arc sweep. Draws into the foreground draw list at `center`. Use during
// launch / blocking ops.
void chrome_loading_ring(
    ImDrawList* dl,
    ImVec2 center,
    float radius,
    float t_seconds,
    ImU32 color = 0);

}  // namespace halox::ui
