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

	// Launcher window is ALWAYS a draggable WS_OVERLAPPEDWINDOW.  We used to
	// honor mcc_user_settings()->graphics.fullscreen_mode here so the launcher
	// inherited MCC's window mode, but that made halox itself non-draggable
	// when MCC was set to borderless — and at 4K with WS_POPUP there was no
	// way to move the window at all.  In-game fullscreen is now driven from
	// the SETTINGS page (deferred change applied via SetWindowLongPtr later);
	// the initial launcher is always a normal sizable window.
	DWORD style = WS_OVERLAPPEDWINDOW;
	int   win_w = g_win32_parameter.window_width;
	int   win_h = g_win32_parameter.window_height;
	// Center on the primary monitor's work area (excludes the taskbar) so
	// the title bar is always reachable. CW_USEDEFAULT could land us with the
	// caption clipped by the top of the screen on some displays.
	RECT  work{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	int   pos_x = work.left + ((work.right  - work.left) - win_w) / 2;
	int   pos_y = work.top  + ((work.bottom - work.top)  - win_h) / 2;
	if (pos_x < work.left) pos_x = work.left;
	if (pos_y < work.top)  pos_y = work.top;

	g_win32_parameter.window_handle = CreateWindowExW(
		0,
		g_win32_parameter.class_name,
		g_win32_parameter.window_name,
		style,
		pos_x,
		pos_y,
		win_w,
		win_h,
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
