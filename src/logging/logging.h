#pragma once

#include <cstdio>

class c_console_logger {
public:
	int initialize();
	int shutdown();

	void debug(const char* fmt, ...);
	void info(const char* fmt, ...);
	void warn(const char* fmt, ...);
	void error(const char* file, int line, const char* fmt, ...);

	void flush();

	static c_console_logger g_console_logger;
private:
	const char* format(char* buffer, size_t size, const char* fmt, va_list args);
	void write_line(const char* prefix, const char* str);
	bool m_initialized = false;
	FILE* m_log_file = nullptr;
};

inline c_console_logger* console_logger() {
	return &c_console_logger::g_console_logger;
}

#define CONSOLE_LOG_DEBUG(fmt, ...) console_logger()->debug(fmt, __VA_ARGS__)
#define CONSOLE_LOG_INFO(fmt, ...) console_logger()->info(fmt, __VA_ARGS__)
#define CONSOLE_LOG_WARN(fmt, ...) console_logger()->warn(fmt, __VA_ARGS__)
#define CONSOLE_LOG_ERROR(fmt, ...) console_logger()->error(__FILE__, __LINE__, fmt, __VA_ARGS__)

// RAII guard that suppresses the vectored exception handler's dbghelp stack
// walk for AVs raised on the current thread. Used by halo1's RT vtable stubs
// when chaining to halo1.dll's GetSubResource/GetMipSubResource — those AV on
// partially-init RTs and the dbghelp walk under concurrent firings corrupts
// dbghelp's internal state. A local SEH __except still catches the AV; we
// just need the first-chance vectored handler to stay silent.
class c_exception_log_suppressor {
public:
	c_exception_log_suppressor();
	~c_exception_log_suppressor();
};
bool exception_log_is_suppressed();

// Quit-cleanup window: when non-zero, AVs whose faulting address lies in
// mss64.dll are quietly redirected to ExitThread() instead of killing the
// whole process. The Miles audio worker thread routinely AVs after we
// TerminateThread the game thread (its internal state is referencing torn-
// down halo2 globals). The worker thread can be sacrificed without harm —
// its only job was producing audio for the game we just quit.
//
// Set on entry to halo2 game-quit teardown; cleared once the module cycle
// completes. RAII helper c_quit_cleanup_guard is the easy way to use it.
extern volatile long g_quit_cleanup_active;

class c_quit_cleanup_guard {
public:
	c_quit_cleanup_guard();
	~c_quit_cleanup_guard();
};

