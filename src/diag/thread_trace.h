#pragma once

#include <Windows.h>

// Captures the parent thread's call stack at every CreateThread call inside
// our process, so that when a child thread (e.g. halo2's game thread spawned
// from initialize_game) crashes, we can show "this thread was created from"
// alongside the crash backtrace.
//
// Without this, a crash dump bottoms out at BaseThreadInitThunk → ntdll which
// tells us nothing about what code requested the thread. With it, we can see
// which engine path constructed the thread that crashed.
//
// Install once at startup before any game DLL is loaded. Idempotent.
void thread_trace_install();

// Look up the captured creation stack for a thread id. Returns the number of
// frames written to `out_frames` (up to `max_frames`). Returns 0 if no record
// exists for that thread id (thread was created before our hook was active,
// or by a kernel-side mechanism we can't intercept).
int thread_trace_get_creator_stack(DWORD thread_id, void** out_frames, int max_frames);

// Returns the thread id of the parent that created `thread_id`, or 0 if
// unknown.
DWORD thread_trace_get_creator_tid(DWORD thread_id);
