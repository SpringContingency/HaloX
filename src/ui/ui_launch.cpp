#include "ui_launch.h"
#include "ui_progress.h"

#include "../main/main.h"
#include "../logging/logging.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

// We need to be able to call into c_game_instance_manager::launch_game_internal
// from the worker thread. That function is private — to avoid touching the
// header, the worker thread is exposed here as a member function the manager
// invokes on itself. We forward through a friend-style trampoline declared
// at the bottom of this file.
namespace halox::ui {

volatile LONG g_launch_in_progress = 0;
volatile LONG g_last_launch_rc     = -999;

// Status text. Worker writes; UI reads. We don't synchronize — readers may
// occasionally see a torn intermediate string but the window title / loading
// label tolerate that. Each write fully overwrites the buffer.
static char  s_status[256] = "";

void ui_launch_set_status(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char tmp[256];
	int n = std::vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0) n = 0;
	if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
	tmp[n] = 0;
	// memcpy is atomic enough for diagnostic display purposes; any race a
	// reader sees here is a non-NUL-safe partial string at worst, and the
	// reader's snprintf below limits to its own buffer.
	std::memcpy(s_status, tmp, (size_t)n + 1);
}

void ui_launch_get_status(char* dst, int dst_size) {
	if (!dst || dst_size <= 0) return;
	int n = (int)std::strlen(s_status);
	if (n >= dst_size) n = dst_size - 1;
	std::memcpy(dst, s_status, (size_t)n);
	dst[n] = 0;
}

// Forward-declared trampoline — implemented in game_instance_manager.win32.cpp
// where the manager's private members are accessible.
extern int ui_launch_run_internal();

static DWORD WINAPI launch_worker_main(LPVOID /*ctx*/) {
	ui_launch_set_status("Starting launch…");
	int rc = 0;
	__try {
		rc = ui_launch_run_internal();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		rc = -888;
		ui_launch_set_status("Launch crashed (SEH) — see halox.log");
		CONSOLE_LOG_ERROR("ui_launch: worker thread caught SEH exception during launch_game_internal");
	}
	InterlockedExchange(&g_last_launch_rc, rc);
	InterlockedExchange(&g_launch_in_progress, 0);

	// Successful launch: keep the loading overlay alive while the game
	// thread loads the scenario. The overlay renders via the game thread's
	// end_frame path, which only fires when g_show_imgui is set. The shell
	// view's render() detects in_game-with-loading and draws the loading
	// screen instead of the pause overlay, then auto-clears g_show_imgui
	// once progress_is_loading() returns false.
	// (progress_mark_game_thread_started is called from ui_launch_kick so
	// the timer reflects time-since-PLAY, not time-since-game-thread.)
	if (rc == 0) {
		InterlockedExchange(&g_show_imgui, TRUE);
	}
	return 0;
}

void ui_launch_kick() {
	if (InterlockedCompareExchange(&g_launch_in_progress, 1, 0) != 0) {
		CONSOLE_LOG_WARN("ui_launch_kick: a launch is already in progress");
		return;
	}
	// Start the load-progress timer NOW (at PLAY click) — not later when
	// the game thread spawns. Otherwise the bar shows nothing for the
	// 1–2s the launch worker spends in set_library_settings /
	// create_game_engine / initialize before initialize_game runs and
	// the game thread starts.
	halox::ui::progress_mark_game_thread_started();
	// Force imgui ON so the loading screen renders the entire time. The
	// shell's render() detects launching/loading and draws the full-window
	// loading overlay, which keeps the game's partial render hidden during
	// the long black-frame phase.
	InterlockedExchange(&g_show_imgui, TRUE);

	HANDLE h = CreateThread(nullptr, 0, launch_worker_main, nullptr, 0, nullptr);
	if (!h) {
		CONSOLE_LOG_ERROR("ui_launch_kick: CreateThread failed (err=%lu); falling back to synchronous", GetLastError());
		// Fallback: run synchronously on this thread. UI will freeze for the
		// duration but at least the launch isn't dropped.
		InterlockedExchange(&g_launch_in_progress, 0);
		int rc = ui_launch_run_internal();
		InterlockedExchange(&g_last_launch_rc, rc);
		return;
	}
	CloseHandle(h);  // detach — worker self-terminates.
}

bool ui_launch_pump() {
	if (InterlockedCompareExchange(&g_launch_in_progress, 0, 0) != 0) {
		// Worker holds the D3D context. Yield to keep message pump cheap.
		// A short sleep is fine; the user-perceived latency for cancellation
		// is bounded by this. 16ms ~ 60Hz tick.
		Sleep(16);
		return true;
	}
	// Launch just finished?  If it failed, re-show imgui so the user sees
	// the launcher with the error code. Successful launches leave imgui
	// hidden — the game thread is rendering its own frames now.
	LONG rc = InterlockedCompareExchange(&g_last_launch_rc, -999, -999);
	if (rc != -999 && rc != 0) {
		// One-shot: clear the result code so we don't keep flipping the flag.
		InterlockedExchange(&g_last_launch_rc, -999);
		InterlockedExchange(&g_show_imgui, TRUE);
		ui_launch_set_status("Launch failed (rc=%d)", (int)rc);
	}
	return false;
}

void ui_launch_render_overlay() {
	// Reserved — currently unused because the UI thread skips rendering
	// during a launch (worker owns D3D). Once we add a serializing lock on
	// the D3D immediate context, this can render the animated loading ring
	// + status text via imgui.
}

}  // namespace halox::ui
