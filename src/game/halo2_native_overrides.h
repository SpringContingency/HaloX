#pragma once

// Halo 2 (Vista/PC port) honors MCC's CustomKeyboardMouseMappingV2 by hooking
// the engine's per-VK "is_key_held" lookup function (FUN_1806D1620). Halo 2
// does NOT have a centralized binding table like Reach (triple int32[81][2]
// polled-VK tables) or halo3 (per-player cache with embedded u16[75]).
// Instead, every gameplay query for a button hard-codes the default VK in the
// call:
//
//     FUN_1806D1620(0x57)   // "is W (forward) held?"
//     FUN_1806D1620(0x52)   // "is R (reload) held?"
//     FUN_1806D1620(0x01)   // "is LMB (fire) held?"  (etc.)
//
// We install a MinHook detour on FUN_1806D1620. Inside the detour we maintain
// a 256-entry VK→VK substitution table built from MCC settings: when the game
// asks for "is default-VK-X held?", we forward the call to the trampoline
// with the user-bound VK instead. This is per-launch (and per-settings-reload)
// — there is no per-frame overwriter we need to fight.
//
// See `project_halo2_profile_bypass.md` memory note for the full layout map.
void halo2_apply_native_overrides();

// Campaign pause-menu actions. Both work by writing pending-action flags
// at DAT_180E70D7C..86 — the actual revert/restart runs on the next game
// thread tick inside the state-change pump (FUN_180679D50). Safe to call
// from any thread; no preconditions beyond "halo2 is the active module
// and a game thread is alive". No-op if halo2 isn't loaded.
//
// RVAs reversed by general-purpose agent on 2026-04-29:
//   FUN_18067A950  — revert single-flag writer (DAT_180E70D7D = 1)
//   FUN_18067A980  — restart-mission flag writer (DAT_180E70D7F = 1)
//   FUN_180866B80  — restart-pending byte (DAT_1818A4334 = 1; called
//                    before FUN_18067A980 by the global-event dispatcher)
void halo2_revert_to_checkpoint();
void halo2_restart_mission();

// Disable + remove the three MinHook detours installed by the per-launch
// install path (is_key_held, poller, dur_read). MUST be called BEFORE
// unload_modules tears halo2.dll out from under MinHook's bookkeeping —
// once the DLL is unmapped, MH_RemoveHook would write to freed memory.
//
// On relaunch, install_is_key_held_hook() (driven by halo2_apply_native_overrides)
// re-runs against the new HMODULE and re-installs cleanly. Without this,
// the "g_hooked_module != nullptr" guard skips the second install and all
// halo2 input goes through the native path with no halox remap.
//
// Idempotent. No-op if hooks were never installed or already removed.
void halo2_uninstall_input_hooks();

// Stop all halo2 audio immediately (Miles/AIL channels + IXAudio2 engine).
// Call from a worker thread BEFORE terminating the game thread on quit-
// to-menu, otherwise the game thread is killed but its audio threads
// keep playing whatever was buffered.
//
// Sequence:
//   1) sets DAT_1815F21C8 = 1   (FUN_1806E5EF0 — block new sounds)
//   2) calls FUN_1806CA4C0      (halo2_stop_all_channels — silence active)
//   3) calls IXAudio2::StopEngine via vtable+0x48 on DAT_181E91248
//      (silences Bink/video audio)
//
// Safe to call from any thread; no halo2 critical section is touched.
// No-op if halo2 isn't loaded.
void halo2_silence_audio();
