#include "ui_chrome.h"

#include "ui_theme.h"

#include <cmath>

namespace halox::ui {

static ImU32 to_u32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

void chrome_corner_brackets(
    ImDrawList* dl, ImVec2 min, ImVec2 max,
    float len, float thickness, ImU32 color) {
	if (!dl) return;
	if (color == 0) color = to_u32(col_accent);

	// Top-left
	dl->AddLine(min, ImVec2(min.x + len, min.y), color, thickness);
	dl->AddLine(min, ImVec2(min.x, min.y + len), color, thickness);
	// Top-right
	dl->AddLine(ImVec2(max.x - len, min.y), ImVec2(max.x, min.y), color, thickness);
	dl->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, thickness);
	// Bottom-left
	dl->AddLine(ImVec2(min.x, max.y - len), ImVec2(min.x, max.y), color, thickness);
	dl->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, thickness);
	// Bottom-right
	dl->AddLine(ImVec2(max.x - len, max.y), ImVec2(max.x, max.y), color, thickness);
	dl->AddLine(ImVec2(max.x, max.y - len), ImVec2(max.x, max.y), color, thickness);
}

void chrome_scanlines(ImDrawList* dl, ImVec2 min, ImVec2 max, int spacing) {
	if (!dl || spacing < 1) return;
	const ImU32 c = to_u32(col_scanline);
	const float w = max.x - min.x;
	const float left = min.x;
	const float right = min.x + w;
	for (float y = min.y; y < max.y; y += (float)spacing) {
		dl->AddLine(ImVec2(left, y), ImVec2(right, y), c, 1.0f);
	}
}

void chrome_vignette(ImDrawList* dl, ImVec2 min, ImVec2 max, float strength) {
	if (!dl) return;
	const float a = strength;
	const ImU32 dark = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, a));
	const ImU32 clear = ImGui::ColorConvertFloat4ToU32(ImVec4(0, 0, 0, 0));
	const float band = (max.y - min.y) * 0.30f;
	// Top fade
	dl->AddRectFilledMultiColor(
		min, ImVec2(max.x, min.y + band),
		dark, dark, clear, clear);
	// Bottom fade
	dl->AddRectFilledMultiColor(
		ImVec2(min.x, max.y - band), max,
		clear, clear, dark, dark);
	// Side fades
	const float sband = (max.x - min.x) * 0.18f;
	dl->AddRectFilledMultiColor(
		min, ImVec2(min.x + sband, max.y),
		dark, clear, clear, dark);
	dl->AddRectFilledMultiColor(
		ImVec2(max.x - sband, min.y), max,
		clear, dark, dark, clear);
}

void chrome_section_header(const char* label) {
	if (!label) return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	float h = ImGui::GetTextLineHeight();
	// Amber notch flush-left of the text.
	const float notch_w = 6.0f;
	const float notch_pad = 8.0f;
	dl->AddRectFilled(
		ImVec2(cursor.x, cursor.y + 2.0f),
		ImVec2(cursor.x + notch_w, cursor.y + h - 2.0f),
		to_u32(col_accent));
	ImGui::Dummy(ImVec2(notch_w + notch_pad, 0));
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, col_accent);
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	// Thin cyan separator under the header.
	ImVec2 sep_min(cursor.x, cursor.y + h + 4.0f);
	ImVec2 sep_max(cursor.x + ImGui::GetContentRegionAvail().x, sep_min.y + 1.0f);
	dl->AddRectFilled(sep_min, sep_max, to_u32(col_border));
	ImGui::Dummy(ImVec2(0, 6.0f));
}

void chrome_loading_ring(
    ImDrawList* dl, ImVec2 center, float radius, float t_seconds, ImU32 color) {
	if (!dl) return;
	if (color == 0) color = to_u32(col_accent);

	const int seg_total = 64;
	const float two_pi = 6.28318530718f;
	const float arc_span = two_pi * 0.32f;          // ~115 degree arc
	const float angle_per_sec = two_pi * 0.55f;     // ~0.55 rev/s
	const float start = std::fmod(t_seconds * angle_per_sec, two_pi);

	const int seg_arc = (int)((arc_span / two_pi) * (float)seg_total);

	// Background ring (cyan, dim).
	const ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(col_border.x, col_border.y, col_border.z, 0.20f));
	dl->AddCircle(center, radius, bg, seg_total, 2.0f);

	// Foreground arc.
	dl->PathClear();
	for (int i = 0; i <= seg_arc; ++i) {
		float a = start + ((float)i / (float)seg_total) * two_pi;
		dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
	}
	dl->PathStroke(color, 0, 3.0f);
}

}  // namespace halox::ui
