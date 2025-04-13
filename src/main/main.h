#pragma once

#include "win32.h"
#include "../thread/thread_safe_queue.h"

#define TEXTA(x) x
#define TEXTW(x) L##x
#define STRINGIFYA(x) #x
#define STRINGIFYW(x) L#x
#define TOSTRINGA(x) STRINGIFYA(x)
#define TOSTRINGW(x) STRINGIFYW(x)

enum e_window_message : UINT {
	_window_message_none = WM_USER,
	_window_message_game_launch,
	_window_message_game_resize,
	_window_message_game_restart,
	_window_message_game_focus,
	_window_message_game_quit,

	k_window_message_count
};

const wchar_t* main_get_root_folder();

extern struct s_win32_parameter {
    HINSTANCE instance_handle;
    WNDPROC window_proc;
    ATOM window_class;
    HWND window_handle;
    DWORD window_thread;
    int window_width;
	int window_height;
    int cmd_show;
    LPSTR cmd_line;
    WCHAR class_name[64];
    WCHAR window_name[64];

    HCURSOR cursor_handle;
	POINT cursor_position;
	bool cursor_capture;
} g_win32_parameter;

extern LONG g_show_imgui_cached;
extern volatile LONG g_show_imgui;
extern volatile LONG g_game_focused;
extern c_fixed_thread_safe_queue<MSG> g_message_queue;

void main_render();
void main_render_game();
void main_render_game_halo3();
void main_render_player(bool* show);
