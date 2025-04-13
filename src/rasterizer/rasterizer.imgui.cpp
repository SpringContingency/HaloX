#include "rasterizer.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>

int c_rasterizer::initialize_imgui() {
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags =
		ImGuiConfigFlags_NavEnableKeyboard |
		ImGuiConfigFlags_NavEnableGamepad |
		ImGuiConfigFlags_DockingEnable;

	ImGui_ImplWin32_Init(g_win32_parameter.window_handle);
	ImGui_ImplDX11_Init(m_d3d11_device, m_d3d11_device_context);

	return 0;
}

int c_rasterizer::shutdown_imgui() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	return 0;
}

int c_rasterizer::begin_frame_imgui() {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	return 0;
}

int c_rasterizer::end_frame_imgui() {
	if (g_win32_parameter.window_thread != GetCurrentThreadId()) {
		copy_surface(_surface_game, _surface_back_buffer);
		clear_surface(_surface_back_buffer, k_color_black);
	}

	set_surface(_surface_back_buffer);
	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return 0;
}
