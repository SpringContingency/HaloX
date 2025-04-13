#include "rasterizer.h"

int c_rasterizer::initialize_window() {
	if (g_win32_parameter.window_handle != nullptr) {
		return 0;
	}

	if (!g_win32_parameter.window_class) {
		WNDCLASSEXW main_window_class = {
			.cbSize = sizeof(WNDCLASSEXW),
			.style = CS_CLASSDC,
			.lpfnWndProc = g_win32_parameter.window_proc,
			.cbClsExtra = 0,
			.cbWndExtra = 0,
			.hInstance = g_win32_parameter.instance_handle,
			.hIcon = nullptr,
			.hCursor = nullptr,
			.hbrBackground = nullptr,
			.lpszMenuName = nullptr,
			.lpszClassName = g_win32_parameter.class_name,
			.hIconSm = 0
		};

		g_win32_parameter.window_class = RegisterClassExW(&main_window_class);

		if (!g_win32_parameter.window_class) {
			return 1;
		}
	}

	DWORD style = WS_OVERLAPPEDWINDOW;

	g_win32_parameter.window_handle = CreateWindowExW(
		0,
		g_win32_parameter.class_name,
		g_win32_parameter.window_name,
		style,
		0,
		0,
		g_win32_parameter.window_width,
		g_win32_parameter.window_height,
		GetDesktopWindow(),
		NULL,
		g_win32_parameter.instance_handle,
		NULL);

	if (!g_win32_parameter.window_handle) {
		return 2;
	}

	ShowWindow(g_win32_parameter.window_handle, g_win32_parameter.cmd_show);
	UpdateWindow(g_win32_parameter.window_handle);

	return 0;
}

int c_rasterizer::shutdown_window() {
	if (g_win32_parameter.window_handle) {
		DestroyWindow(g_win32_parameter.window_handle);
		g_win32_parameter.window_handle = nullptr;
	}

	if (g_win32_parameter.window_class) {
		UnregisterClassW(g_win32_parameter.class_name, GetModuleHandleW(nullptr));
		g_win32_parameter.window_class = 0;
	}

	return 0;
}
