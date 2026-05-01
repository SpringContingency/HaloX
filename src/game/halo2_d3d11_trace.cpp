// Diagnostic instrumentation for halo2 launch-time d3d11 AV at +0x1059C5.
//
// Crash chain (from halox.log call stacks):
//   FUN_180103290+0x45  →  vt[0x30] of *(param_2 + 0xd58)  →  d3d11.dll AV
//
// FUN_180103290 is a tiny function reachable only via vtable dispatch
// (slot +0x38 / index 7 of param_2's vtable in the parent FUN_18010ED70).
// Its body:
//
//   if (param_1[8] < param_1[0xc]) {
//     (**(*(param_2 + 0xd58) + 0x180))(*(param_2 + 0xd58),
//                                       *(param_1 + 0x28), 0, 0,
//                                       *(param_1 + 0x18), 0, 0);
//     ...
//   }
//
// `*(param_2 + 0xd58)` is dereferenced for its vtable. If that pointer is
// null or stale, the chain crashes inside d3d11. This module hooks
// FUN_180103290's entry, logs the first few calls' state, and short-
// circuits the call when the field is obviously bad — preventing the AV
// so we can see how far halo2 gets without it.

#include "halo2_d3d11_trace.h"

#include "../logging/logging.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include <MinHook.h>

namespace {

constexpr uintptr_t k_h2_render_call_rva = 0x103290;

using RenderCallFn = void (__fastcall *)(uintptr_t param_1, uintptr_t param_2);

RenderCallFn g_render_call_orig    = nullptr;
HMODULE      g_render_call_module  = nullptr;
bool         g_minhook_initialized = false;
std::atomic<int> g_render_call_count{0};
std::atomic<int> g_render_call_skipped{0};

bool ptr_is_readable(const void* p, size_t n) {
	if (!p) return false;
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT) return false;
	const DWORD bad = PAGE_NOACCESS | PAGE_GUARD;
	if (mbi.Protect & bad) return false;
	const auto base = reinterpret_cast<uintptr_t>(p);
	const auto end  = base + n;
	const auto region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	return end <= region_end;
}

void __fastcall render_call_detour(uintptr_t param_1, uintptr_t param_2) {
	const int n = g_render_call_count.fetch_add(1, std::memory_order_relaxed) + 1;

	uintptr_t field_d58 = 0;
	uintptr_t field_vt  = 0;
	bool param_2_ok     = ptr_is_readable(reinterpret_cast<void*>(param_2 + 0xd58), 8);
	bool field_ok       = false;
	bool vt_ok          = false;

	if (param_2_ok) {
		field_d58 = *reinterpret_cast<uintptr_t*>(param_2 + 0xd58);
		field_ok  = ptr_is_readable(reinterpret_cast<void*>(field_d58), 8);
		if (field_ok) {
			field_vt = *reinterpret_cast<uintptr_t*>(field_d58);
			vt_ok    = ptr_is_readable(reinterpret_cast<void*>(field_vt + 0x180), 8);
		}
	}

	// First few calls, plus every 1024 thereafter, plus any "bad" call.
	const bool log_this = (n <= 6) || (n % 1024 == 0) || !param_2_ok || !field_ok || !vt_ok;
	if (log_this) {
		CONSOLE_LOG_INFO("h2_d3d_trace: FUN_180103290 call#%d p1=0x%llX p2=0x%llX "
			"p2+0xd58_readable=%d *(p2+0xd58)=0x%llX field_readable=%d "
			"*field=0x%llX vt+0x180_readable=%d",
			n, (unsigned long long)param_1, (unsigned long long)param_2,
			(int)param_2_ok, (unsigned long long)field_d58, (int)field_ok,
			(unsigned long long)field_vt, (int)vt_ok);
	}

	// LOG-ONLY MODE (Anniversary diagnostic): we no longer suppress the call
	// when the chain looks bad. The original launch-time AV only fired with
	// a specific stale-pointer pattern that's been worked around elsewhere;
	// the suppression path was killing legitimate Anniversary draws (whose
	// vtable layout is similar but valid) and producing a world-less HUD-
	// only render. Now we always defer to the engine and just record the
	// pointer state so we can correlate with what's visible. If a real AV
	// resurfaces, the unhandled-exception filter still catches it.
	if (!param_2_ok || !field_ok || !vt_ok) {
		const int skipped = g_render_call_skipped.fetch_add(1, std::memory_order_relaxed) + 1;
		if (skipped <= 4 || (skipped % 64) == 0) {
			CONSOLE_LOG_WARN("h2_d3d_trace: WOULD-SKIP FUN_180103290 call#%d "
				"(would-skip#%d, p2_ok=%d field_ok=%d vt_ok=%d) — "
				"calling engine anyway (Anniversary diagnostic)",
				n, skipped, (int)param_2_ok, (int)field_ok, (int)vt_ok);
		}
	}
	if (g_render_call_orig) {
		g_render_call_orig(param_1, param_2);
	}
}

} // namespace

void halo2_d3d11_trace_install() {
	if (g_render_call_module) return;

	HMODULE mod = GetModuleHandleW(L"halo2.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("h2_d3d_trace: halo2.dll not loaded — skipping install");
		return;
	}

	if (!g_minhook_initialized) {
		MH_STATUS st = MH_Initialize();
		if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			CONSOLE_LOG_WARN("h2_d3d_trace: MH_Initialize failed (status=%d)", (int)st);
			return;
		}
		g_minhook_initialized = true;
	}

	void* target = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mod) + k_h2_render_call_rva);
	void* tramp  = nullptr;
	MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&render_call_detour), &tramp);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("h2_d3d_trace: CreateHook @ %p failed (status=%d)", target, (int)st);
		return;
	}
	g_render_call_orig = reinterpret_cast<RenderCallFn>(tramp);
	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_WARN("h2_d3d_trace: EnableHook @ %p failed (status=%d)", target, (int)st);
		g_render_call_orig = nullptr;
		return;
	}
	g_render_call_module = mod;
	CONSOLE_LOG_INFO("h2_d3d_trace: hook installed @ %p (halo2+0x%llX) — instrumenting FUN_180103290",
		target, (unsigned long long)k_h2_render_call_rva);
}
