#include "logging.h"

#include <Windows.h>
#include <cstdio>

static SRWLOCK g_console_lock = SRWLOCK_INIT;

c_console_logger c_console_logger::g_console_logger;

int c_console_logger::initialize() {
	FILE* file;

	// check if console already exists
	if (GetConsoleWindow != NULL) {
		m_initialized = true;
		return 0; // console already exists
	}

	// allocate console
	AllocConsole();

	// redirect stdin, stdout, stderr
	freopen_s(&file, "CONIN$", "r", stdin);
	freopen_s(&file, "CONOUT$", "w", stdout);
	freopen_s(&file, "CONOUT$", "w", stderr);

	g_console_lock = SRWLOCK_INIT;

	m_initialized = true;
	return 0;
}

int c_console_logger::shutdown() {
	// check if console exists
	if (GetConsoleWindow == NULL) {
		m_initialized = false;
		return 0; // console does not exist
	}

	FreeConsole();

	// close stdin, stdout, stderr
	fclose(stdin);
	fclose(stdout);
	fclose(stderr);

	m_initialized = false;
	return 0;
}

void c_console_logger::debug(const char* fmt, ...) {
#ifdef _DEBUG
	int len;
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) {
		return;
	}

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) {
		return;
	}

	AcquireSRWLockExclusive(&g_console_lock);
	OutputDebugStringA(str);
	fprintf(stdout, "[DEBUG] %s", str);
	ReleaseSRWLockExclusive(&g_console_lock);
#endif
}

void c_console_logger::info(const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) {
		return;
	}

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) {
		return;
	}

	AcquireSRWLockExclusive(&g_console_lock);
	fprintf(stdout, "[INFO] %s", str);
	ReleaseSRWLockExclusive(&g_console_lock);
}

void c_console_logger::warn(const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) {
		return;
	}

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) {
		return;
	}

	AcquireSRWLockExclusive(&g_console_lock);
	fprintf(stdout, "[WARN] %s", str);
	ReleaseSRWLockExclusive(&g_console_lock);
}

void c_console_logger::error(const char* file, int line, const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) {
		return;
	}

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) {
		return;
	}

	AcquireSRWLockExclusive(&g_console_lock);
	fprintf(stderr, "[ERROR] %s:%d: %s", file, line, str);
	ReleaseSRWLockExclusive(&g_console_lock);
}

const char* c_console_logger::format(
	char* buffer, 
	size_t size, 
	const char* fmt, 
	va_list args
) {
	int len;

	len = vsnprintf_s(buffer, size, size - 1, fmt, args);

	if (len < 0) {
		return nullptr;
	}

	if (len >= size) {
		return nullptr;
	}

	buffer[len - 1] = '\n';
	buffer[len] = 0;

	return buffer;
}
