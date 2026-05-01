#include "thread_trace.h"

#include "../logging/logging.h"

#include <Windows.h>
#include <MinHook.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace {

constexpr int kMaxFrames = 24;

struct s_creator_record {
	DWORD parent_tid = 0;
	int   frame_count = 0;
	void* frames[kMaxFrames] = {};
};

std::unordered_map<DWORD, s_creator_record> g_records;
SRWLOCK g_records_lock = SRWLOCK_INIT;

// Save a record. Capacity-bounded to keep the map small even for processes
// that churn many threads — drop the oldest opportunistically. We don't need
// strict LRU; we just need to bound memory.
void save_record(DWORD child_tid, DWORD parent_tid, void** frames, int frame_count) {
	AcquireSRWLockExclusive(&g_records_lock);
	if (g_records.size() > 4096) {
		// Cheap eviction: drop the first ~1024 entries. The traces we care
		// about (game thread creation) happen early in launch and are highly
		// unlikely to be evicted under any reasonable scenario.
		int dropped = 0;
		for (auto it = g_records.begin(); it != g_records.end() && dropped < 1024; ) {
			it = g_records.erase(it);
			dropped++;
		}
	}
	auto& rec = g_records[child_tid];
	rec.parent_tid = parent_tid;
	rec.frame_count = frame_count > kMaxFrames ? kMaxFrames : frame_count;
	for (int i = 0; i < rec.frame_count; i++) rec.frames[i] = frames[i];
	ReleaseSRWLockExclusive(&g_records_lock);
}

// CreateThread detour. Captures the caller's stack, calls original, then
// records the new thread id under the captured stack.

using CreateThreadFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
	LPVOID, DWORD, LPDWORD);
CreateThreadFn g_orig_create_thread = nullptr;

HANDLE WINAPI hook_CreateThread(
	LPSECURITY_ATTRIBUTES sa, SIZE_T stack_size,
	LPTHREAD_START_ROUTINE start, LPVOID arg,
	DWORD flags, LPDWORD out_tid)
{
	// Snapshot the caller's stack BEFORE delegating — this is the return path
	// inside the engine that requested the thread.
	void* frames[kMaxFrames];
	int captured = (int)RtlCaptureStackBackTrace(/*skip*/1, kMaxFrames, frames, nullptr);
	DWORD parent_tid = GetCurrentThreadId();

	DWORD local_tid = 0;
	HANDLE h = g_orig_create_thread(sa, stack_size, start, arg, flags, out_tid ? out_tid : &local_tid);
	if (h) {
		DWORD child_tid = out_tid ? *out_tid : local_tid;
		if (child_tid) save_record(child_tid, parent_tid, frames, captured);
	}
	return h;
}

bool g_installed = false;

} // namespace

void thread_trace_install() {
	if (g_installed) return;
	g_installed = true;

	MH_STATUS st = MH_Initialize();
	if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
		CONSOLE_LOG_WARN("thread_trace: MH_Initialize failed (status=%d) — thread creator stacks won't be captured", (int)st);
		return;
	}

	HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
	if (!k32) {
		CONSOLE_LOG_WARN("thread_trace: kernel32.dll not loaded?");
		return;
	}
	void* target = reinterpret_cast<void*>(GetProcAddress(k32, "CreateThread"));
	if (!target) {
		CONSOLE_LOG_WARN("thread_trace: CreateThread export not found");
		return;
	}

	void* trampoline = nullptr;
	st = MH_CreateHook(target, reinterpret_cast<void*>(&hook_CreateThread), &trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("thread_trace: MH_CreateHook failed (status=%d)", (int)st);
		return;
	}
	g_orig_create_thread = reinterpret_cast<CreateThreadFn>(trampoline);
	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("thread_trace: MH_EnableHook failed (status=%d)", (int)st);
		return;
	}

	CONSOLE_LOG_INFO("thread_trace: CreateThread hook installed — child thread crashes will include creator stack");
}

int thread_trace_get_creator_stack(DWORD thread_id, void** out_frames, int max_frames) {
	AcquireSRWLockShared(&g_records_lock);
	auto it = g_records.find(thread_id);
	if (it == g_records.end()) {
		ReleaseSRWLockShared(&g_records_lock);
		return 0;
	}
	int n = it->second.frame_count < max_frames ? it->second.frame_count : max_frames;
	for (int i = 0; i < n; i++) out_frames[i] = it->second.frames[i];
	ReleaseSRWLockShared(&g_records_lock);
	return n;
}

DWORD thread_trace_get_creator_tid(DWORD thread_id) {
	AcquireSRWLockShared(&g_records_lock);
	auto it = g_records.find(thread_id);
	DWORD parent = (it == g_records.end()) ? 0 : it->second.parent_tid;
	ReleaseSRWLockShared(&g_records_lock);
	return parent;
}
