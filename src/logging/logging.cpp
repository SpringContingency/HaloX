#include "logging.h"

#include "../diag/thread_trace.h"

#include <Windows.h>
#include <Psapi.h>
#include <DbgHelp.h>
#include <share.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "Psapi.lib")

static SRWLOCK g_console_lock = SRWLOCK_INIT;

c_console_logger c_console_logger::g_console_logger;

// Per-thread suppression flag: when non-zero, the first-chance vectored
// exception handler returns immediately without doing a dbghelp stack walk.
// Used by code paths that intentionally chain into native code which may AV
// (e.g. halo1 RT vt[0x1A]/[0x1B] on partially-init RTs); the local SEH
// __except still catches the AV — we just need the vectored handler to
// stay silent so concurrent firings don't corrupt dbghelp state.
static thread_local int g_exception_log_suppress_depth = 0;

c_exception_log_suppressor::c_exception_log_suppressor() {
	g_exception_log_suppress_depth++;
}
c_exception_log_suppressor::~c_exception_log_suppressor() {
	g_exception_log_suppress_depth--;
}
bool exception_log_is_suppressed() {
	return g_exception_log_suppress_depth > 0;
}

volatile long g_quit_cleanup_active = 0;

c_quit_cleanup_guard::c_quit_cleanup_guard() {
	InterlockedIncrement(&g_quit_cleanup_active);
}
c_quit_cleanup_guard::~c_quit_cleanup_guard() {
	InterlockedDecrement(&g_quit_cleanup_active);
}

static bool addr_in_module(uintptr_t addr, const wchar_t* leafname) {
	HMODULE m = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(addr), &m) || !m) return false;
	wchar_t path[MAX_PATH] = { 0 };
	if (!GetModuleFileNameW(m, path, MAX_PATH)) return false;
	const wchar_t* leaf = wcsrchr(path, L'\\');
	leaf = leaf ? leaf + 1 : path;
	return _wcsicmp(leaf, leafname) == 0;
}

static void describe_address(uintptr_t addr, char* out, size_t out_size) {
	HMODULE m = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(addr), &m) || !m) {
		_snprintf_s(out, out_size, _TRUNCATE, "0x%016llX  <unknown>", (uint64_t)addr);
		return;
	}
	wchar_t modw[MAX_PATH] = { 0 };
	GetModuleFileNameW(m, modw, MAX_PATH);
	const wchar_t* leaf = wcsrchr(modw, L'\\');
	leaf = leaf ? leaf + 1 : modw;
	char modname[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, leaf, -1, modname, sizeof(modname), nullptr, nullptr);
	uintptr_t base = (uintptr_t)m;

	// try to resolve symbol via DbgHelp (works for halox.exe with PDB; halo3.dll has no PDB so falls back to RVA)
	char symbuf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME + 1] = { 0 };
	auto sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = MAX_SYM_NAME;
	DWORD64 displ = 0;
	if (SymFromAddr(GetCurrentProcess(), addr, &displ, sym) && sym->Name[0]) {
		_snprintf_s(out, out_size, _TRUNCATE,
			"0x%016llX  %s+0x%llX  (%s!%s+0x%llX)",
			(uint64_t)addr, modname, (uint64_t)(addr - base), modname, sym->Name, (uint64_t)displ);
	} else {
		_snprintf_s(out, out_size, _TRUNCATE,
			"0x%016llX  %s+0x%llX",
			(uint64_t)addr, modname, (uint64_t)(addr - base));
	}
}

static void log_stack_trace(CONTEXT* ctx) {
	HANDLE proc = GetCurrentProcess();
	HANDLE thread = GetCurrentThread();

	// Symbol search path: exe dir + halo3 dir
	wchar_t exe_dir_w[MAX_PATH];
	GetModuleFileNameW(nullptr, exe_dir_w, MAX_PATH);
	if (auto p = wcsrchr(exe_dir_w, L'\\')) *p = 0;
	char sympath[MAX_PATH * 4];
	char exe_dir[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, exe_dir_w, -1, exe_dir, sizeof(exe_dir), nullptr, nullptr);
	_snprintf_s(sympath, sizeof(sympath), _TRUNCATE,
		"%s;%s\\halo3;srv*%s\\symcache*https://msdl.microsoft.com/download/symbols",
		exe_dir, exe_dir, exe_dir);

	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	SymInitialize(proc, sympath, TRUE);

	console_logger()->error(__FILE__, __LINE__, "RIP context:");
	char line[1024];
	describe_address((uintptr_t)ctx->Rip, line, sizeof(line));
	console_logger()->error(__FILE__, __LINE__, "  RIP = %s", line);
	console_logger()->error(__FILE__, __LINE__,
		"  RSP = 0x%016llX  RBP = 0x%016llX",
		(uint64_t)ctx->Rsp, (uint64_t)ctx->Rbp);
	console_logger()->error(__FILE__, __LINE__,
		"  RAX = 0x%016llX  RCX = 0x%016llX  RDX = 0x%016llX",
		(uint64_t)ctx->Rax, (uint64_t)ctx->Rcx, (uint64_t)ctx->Rdx);
	console_logger()->error(__FILE__, __LINE__,
		"  R8  = 0x%016llX  R9  = 0x%016llX  R10 = 0x%016llX",
		(uint64_t)ctx->R8, (uint64_t)ctx->R9, (uint64_t)ctx->R10);

	// If RIP is null/garbage (typical for "called null function pointer" crashes),
	// the actual return address is at the top of the stack. Recover it and prime
	// the walk so StackWalk64 has something to anchor on.
	CONTEXT walk_ctx = *ctx;
	if (walk_ctx.Rip == 0) {
		uintptr_t ret_at_rsp = 0;
		__try {
			ret_at_rsp = *reinterpret_cast<uintptr_t*>(walk_ctx.Rsp);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			ret_at_rsp = 0;
		}
		if (ret_at_rsp) {
			describe_address(ret_at_rsp, line, sizeof(line));
			console_logger()->error(__FILE__, __LINE__,
				"  RIP was null; caller (return addr at RSP) = %s", line);
			walk_ctx.Rip = ret_at_rsp;
			walk_ctx.Rsp += 8;
		}
	}

	// Raw stack dump — catches frames StackWalk can't reconstruct without unwind info.
	console_logger()->error(__FILE__, __LINE__, "Stack memory @ RSP:");
	for (int i = 0; i < 24; ++i) {
		uintptr_t sp = ctx->Rsp + i * 8;
		uintptr_t v = 0;
		__try { v = *reinterpret_cast<uintptr_t*>(sp); } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
		if (v == 0) {
			console_logger()->error(__FILE__, __LINE__, "  [RSP+0x%02X] 0x%016llX", i * 8, (uint64_t)v);
			continue;
		}
		HMODULE m = nullptr;
		if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(v), &m) && m) {
			char desc[512];
			describe_address(v, desc, sizeof(desc));
			console_logger()->error(__FILE__, __LINE__, "  [RSP+0x%02X] %s", i * 8, desc);
		} else {
			console_logger()->error(__FILE__, __LINE__, "  [RSP+0x%02X] 0x%016llX", i * 8, (uint64_t)v);
		}
	}

	// walk stack (primed from caller's return address if RIP was null)
	STACKFRAME64 frame = {};
	frame.AddrPC.Offset    = walk_ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
	frame.AddrFrame.Offset = walk_ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = walk_ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;

	console_logger()->error(__FILE__, __LINE__, "Call stack:");
	for (int i = 0; i < 64; ++i) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, &walk_ctx,
			nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
			break;
		}
		if (frame.AddrPC.Offset == 0) break;
		describe_address((uintptr_t)frame.AddrPC.Offset, line, sizeof(line));
		console_logger()->error(__FILE__, __LINE__, "  #%02d  %s", i, line);
	}

	// If thread_trace recorded a creator stack for this thread, walk that
	// chain too — it tells us which engine path constructed the thread that
	// crashed. Without this, child-thread backtraces bottom out at
	// BaseThreadInitThunk → ntdll which is useless context.
	{
		DWORD this_tid = GetCurrentThreadId();
		void* creator_frames[24];
		int creator_count = thread_trace_get_creator_stack(this_tid, creator_frames, 24);
		DWORD parent_tid = thread_trace_get_creator_tid(this_tid);
		if (creator_count > 0) {
			console_logger()->error(__FILE__, __LINE__,
				"Thread creator stack (parent tid=%lu):", parent_tid);
			for (int i = 0; i < creator_count; i++) {
				describe_address(reinterpret_cast<uintptr_t>(creator_frames[i]), line, sizeof(line));
				console_logger()->error(__FILE__, __LINE__, "  ^%02d  %s", i, line);
			}
		}
	}

	SymCleanup(proc);
	console_logger()->flush();
}

static LONG WINAPI halox_unhandled_exception_filter(EXCEPTION_POINTERS* info) {
	auto code = info->ExceptionRecord->ExceptionCode;
	auto addr = info->ExceptionRecord->ExceptionAddress;

	HMODULE faulting_mod = nullptr;
	wchar_t mod_path[MAX_PATH] = { 0 };
	uintptr_t mod_base = 0;
	if (GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(addr),
		&faulting_mod) && faulting_mod) {
		GetModuleFileNameW(faulting_mod, mod_path, MAX_PATH);
		MODULEINFO mi{};
		if (GetModuleInformation(GetCurrentProcess(), faulting_mod, &mi, sizeof(mi))) {
			mod_base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
		}
	}

	console_logger()->error(__FILE__, __LINE__,
		"!!! UNHANDLED EXCEPTION code=0x%08lX addr=0x%p module=%ls base=0x%p rva=0x%llX thread=%lu",
		code, addr, mod_path[0] ? mod_path : L"<unknown>",
		(void*)mod_base,
		mod_base ? (uint64_t)((uintptr_t)addr - mod_base) : 0ull,
		GetCurrentThreadId());

	if (info->ContextRecord) {
		CONTEXT ctx_copy = *info->ContextRecord;
		log_stack_trace(&ctx_copy);
	}

	console_logger()->flush();

	return EXCEPTION_EXECUTE_HANDLER;  // let the process die
}

// Returns true if the faulting thread should be redirected to ExitThread
// instead of crashing the process. Currently triggers only during the quit-
// cleanup window for mss64.dll AVs (Miles Sound System worker thread).
static bool maybe_redirect_to_exit_thread(EXCEPTION_POINTERS* info) {
	if (!info || !info->ExceptionRecord || !info->ContextRecord) return false;
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return false;
	if (!g_quit_cleanup_active) return false;
	auto addr = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
	if (!addr_in_module(addr, L"mss64.dll")) return false;
	console_logger()->warn(
		"quit-cleanup: redirecting mss64.dll thread %lu RIP=0x%llX to ExitThread (AV swallowed)",
		GetCurrentThreadId(), (uint64_t)addr);
	console_logger()->flush();
	auto* ctx = info->ContextRecord;
	ctx->Rip = reinterpret_cast<DWORD64>(&ExitThread);
	ctx->Rcx = 0;  // exit code passed to ExitThread
	return true;
}

int c_console_logger::initialize() {
	FILE* file;

	if (GetConsoleWindow() == NULL) {
		AllocConsole();
		freopen_s(&file, "CONIN$", "r", stdin);
		freopen_s(&file, "CONOUT$", "w", stdout);
		freopen_s(&file, "CONOUT$", "w", stderr);
	}

	// Tee everything to halox.log next to the executable so we can read it
	// after a crash takes the console window down. Open with shared read so
	// other tools can tail the log while halox is still running.
	//
	// HALOX_INSTANCE env var (default empty = "halox.log") suffixes the
	// filename so two halox processes can run side-by-side without
	// interleaving log writes. e.g. HALOX_INSTANCE=A → halox-A.log
	wchar_t log_path[MAX_PATH];
	GetModuleFileNameW(nullptr, log_path, MAX_PATH);
	if (auto last_slash = wcsrchr(log_path, L'\\')) *(last_slash + 1) = 0;
	wchar_t instance_buf[16] = {};
	DWORD got_inst = GetEnvironmentVariableW(L"HALOX_INSTANCE", instance_buf, 16);
	if (got_inst > 0 && got_inst < 16) {
		wcscat_s(log_path, L"halox-");
		wcscat_s(log_path, instance_buf);
		wcscat_s(log_path, L".log");
	} else {
		wcscat_s(log_path, L"halox.log");
	}
	// Append mode so a crash in a previous run isn't wiped on restart — the
	// last lines before exit are exactly the ones we need to diagnose. Header
	// line below makes it easy to find the start of the current run.
	m_log_file = _wfsopen(log_path, L"a", _SH_DENYWR);
	if (m_log_file) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		fprintf(m_log_file,
			"\n========== halox start %04d-%02d-%02d %02d:%02d:%02d ==========\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		fflush(m_log_file);
	}

	g_console_lock = SRWLOCK_INIT;

	// Vectored handler runs before the SEH frame walk; only forward the
	// "real" hardware/structured exceptions, not C++ throws or DLL-load probes.
	AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* info) -> LONG {
		auto code = info->ExceptionRecord->ExceptionCode;
		switch (code) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
			break;
		default:
			return EXCEPTION_CONTINUE_SEARCH;
		}
		// Skip the dbghelp walk if a code path on this thread has marked
		// itself as "expects to throw and recover via local __except". The
		// process keeps running; only the first-chance log entry is dropped.
		if (exception_log_is_suppressed()) {
			return EXCEPTION_CONTINUE_SEARCH;
		}
		halox_unhandled_exception_filter(info);
		// During halo2 quit cleanup, mss64 worker thread AVs would otherwise
		// kill the whole launcher. Redirect the faulting thread to ExitThread
		// and resume from the modified context — process keeps running.
		if (maybe_redirect_to_exit_thread(info)) {
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		return EXCEPTION_CONTINUE_SEARCH;  // still let SEH handlers (and the unhandled filter) run
	});

	SetUnhandledExceptionFilter(halox_unhandled_exception_filter);

	m_initialized = true;
	return 0;
}

int c_console_logger::shutdown() {
	if (m_log_file) {
		fflush(m_log_file);
		fclose(m_log_file);
		m_log_file = nullptr;
	}

	if (GetConsoleWindow() == NULL) {
		m_initialized = false;
		return 0;
	}

	FreeConsole();

	fclose(stdin);
	fclose(stdout);
	fclose(stderr);

	m_initialized = false;
	return 0;
}

void c_console_logger::flush() {
	AcquireSRWLockExclusive(&g_console_lock);
	if (stdout) fflush(stdout);
	if (stderr) fflush(stderr);
	if (m_log_file) fflush(m_log_file);
	ReleaseSRWLockExclusive(&g_console_lock);
}

void c_console_logger::write_line(const char* prefix, const char* str) {
	AcquireSRWLockExclusive(&g_console_lock);
	OutputDebugStringA(prefix);
	OutputDebugStringA(str);
	fprintf(stdout, "%s%s", prefix, str);
	fflush(stdout);
	if (m_log_file) {
		fprintf(m_log_file, "%s%s", prefix, str);
		fflush(m_log_file);
	}
	ReleaseSRWLockExclusive(&g_console_lock);
}

void c_console_logger::debug(const char* fmt, ...) {
#ifdef _DEBUG
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) return;

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) return;
	write_line("[DEBUG] ", str);
#endif
}

void c_console_logger::info(const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) return;

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) return;
	write_line("[INFO] ", str);
}

void c_console_logger::warn(const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];

	if (!m_initialized) return;

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) return;
	write_line("[WARN] ", str);
}

void c_console_logger::error(const char* file, int line, const char* fmt, ...) {
	va_list args;
	const char* str;
	char buffer[4096];
	char prefix[512];

	if (!m_initialized) return;

	va_start(args, fmt);
	str = format(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (str == nullptr) return;

	_snprintf_s(prefix, sizeof(prefix), _TRUNCATE, "[ERROR] %s:%d: ", file, line);
	write_line(prefix, str);
}

const char* c_console_logger::format(
	char* buffer,
	size_t size,
	const char* fmt,
	va_list args
) {
	int len;

	len = vsnprintf_s(buffer, size, size - 1, fmt, args);

	if (len < 0) return nullptr;
	if (len >= (int)size) return nullptr;

	// Append newline (replacing any caller-supplied trailing one).
	if (len > 0 && buffer[len - 1] == '\n') {
		// already has one
	} else {
		if (len + 1 < (int)size) {
			buffer[len] = '\n';
			buffer[len + 1] = 0;
		}
	}

	return buffer;
}
