#include "halo4_native_overrides.h"

#include "game_instance_manager.h"
#include "../logging/logging.h"

#include <Windows.h>
#include <cstdint>

#include <MinHook.h>

using namespace libmcc;

// halo4.dll FUN_1800785CC — engine-internal "register-resource-into-pool"
// helper, called from FUN_18003E230 during the shared-scenario preload state
// machine (case 1: g_state @ halo4+0x112CD3C == 1). Crashes at +0x78640 on
// `movups xmm0, [rdi+0x10]` when the first argument is null/unmapped.
//
// Caller pattern:
//   ...
//   call 0x1800785CC
//   test rax, rax
//   je   exit              ; 0 = "not registered" → caller exits gracefully
//
// So returning 0 from a guarded version is safe. The function's normal return
// is a non-zero pool-entry pointer; 0 is its existing "no-op" sentinel.
static constexpr uintptr_t k_h4_resource_register_rva = 0x785CC;

using ResourceRegisterFn = uint64_t (__fastcall *)(void* /*rcx*/, uint64_t /*rdx*/);
static ResourceRegisterFn g_h4_orig = nullptr;

static uint64_t __fastcall h4_resource_register_detour(void* arg1, uint64_t arg2) {
	if (arg1 == nullptr) {
		return 0;
	}
	__try {
		return g_h4_orig(arg1, arg2);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return 0;
	}
}

void halo4_install_resource_register_guard() {
	auto im = game_instance_manager();
	if (!im) return;
	if (im->get_game() != _module_halo4) return;

	if (g_h4_orig != nullptr) return;  // idempotent

	HMODULE mod = GetModuleHandleW(L"halo4.dll");
	if (!mod) {
		CONSOLE_LOG_WARN("halo4 resource_register guard: halo4.dll not loaded");
		return;
	}

	auto* target = reinterpret_cast<void*>(
		reinterpret_cast<uintptr_t>(mod) + k_h4_resource_register_rva);

	if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
		// MH_CreateHook will surface a real error below if MH isn't ready.
	}

	void* trampoline = nullptr;
	auto st = MH_CreateHook(target,
		reinterpret_cast<void*>(&h4_resource_register_detour),
		&trampoline);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo4 resource_register guard: MH_CreateHook failed (status=%d) @ %p",
			(int)st, target);
		return;
	}
	g_h4_orig = reinterpret_cast<ResourceRegisterFn>(trampoline);

	st = MH_EnableHook(target);
	if (st != MH_OK) {
		CONSOLE_LOG_ERROR("halo4 resource_register guard: MH_EnableHook failed (status=%d) @ %p",
			(int)st, target);
		return;
	}

	CONSOLE_LOG_INFO("halo4 resource_register guard installed @ %p (halo4+0x%llX)",
		target, (unsigned long long)k_h4_resource_register_rva);
}
