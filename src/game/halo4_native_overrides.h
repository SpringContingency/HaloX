#pragma once

// halo4.dll campaign launch crashes inside FUN_180078640 (call site at
// halo4.dll+0x785CC), which is the engine's per-resource registration hook
// for the shared-scenario preload state machine.
//
// Crash: AV at halo4.dll+0x78640 on `movups xmm0, [rdi+0x10]` — the function
// dereferences its first argument (saved into rdi at the prologue) without a
// null check. The argument arrives from a vfunc + helper chain in the caller
// (FUN_18003E230) and ends up null/unmapped because halox does not perform
// the same pre-launch shared-tag preload that MCC does.
//
// The call site already handles a 0 return value gracefully:
//   call 0x1800785CC
//   test rax, rax
//   je   exit          ; 0 → safe exit
//
// So we install a MinHook detour at +0x785CC that:
//   1. Returns 0 directly if rcx == nullptr (the common case).
//   2. Otherwise runs the original under SEH and returns 0 on AV.
//
// Skipping the resource-register call leaves the shared scenarios partially
// preloaded, but the engine's caller treats that as a no-op and continues —
// the campaign launch then proceeds past this wall.
//
// Install once per halo4 launch, AFTER halo4.dll is loaded and BEFORE the
// game thread spawns. Idempotent.
void halo4_install_resource_register_guard();
