#include "game_manager.h"

#include "../main/main.h"
#include "../logging/logging.h"
#include "game_instance_manager.h"

#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

using namespace libmcc;

void __fastcall c_game_manager::set_game_state(e_game_state state) {
	CONSOLE_LOG_DEBUG("set_game_state:%d", state);
}

// When halo3 (or any module) reports restart_game(reason=0, message=NULL) the
// useful question is: where in the DLL did the call originate? Capture a few
// frames of the C++ caller stack and resolve each to module+offset so we can
// take the address back to Ghidra and identify the bail-out site.
static void log_restart_callstack() {
	void* frames[16] = {};
	USHORT count = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
	for (USHORT i = 0; i < count; ++i) {
		HMODULE mod = nullptr;
		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)frames[i], &mod)) {
			CONSOLE_LOG_DEBUG("  frame[%u] %p (?)", (unsigned)i, frames[i]);
			continue;
		}
		wchar_t name[MAX_PATH] = L"?";
		GetModuleFileNameW(mod, name, MAX_PATH);
		const wchar_t* leaf = wcsrchr(name, L'\\');
		leaf = leaf ? leaf + 1 : name;
		uintptr_t base = (uintptr_t)mod;
		uintptr_t off  = (uintptr_t)frames[i] - base;
		CONSOLE_LOG_DEBUG("  frame[%u] %ls+0x%llX (%p)", (unsigned)i, leaf,
			(unsigned long long)off, frames[i]);
	}
}

void __fastcall c_game_manager::restart_game(
	e_game_restart_reason reason,
	const char* message
) {
	if (message == nullptr) {
		CONSOLE_LOG_DEBUG("restart_game(reason=%d, message=NULL) — capturing callstack:", (int)reason);
		log_restart_callstack();
	}
	PostMessageW(
		g_win32_parameter.window_handle,
		_window_message_game_restart,
		reason,
		reinterpret_cast<LPARAM>(message));
}

uintptr_t __fastcall libmcc::c_game_manager::update_launch_timer(
	int a2, 
	float a3
) {
	m_game_loading_progress = a3;

	return uintptr_t();
}

void __fastcall c_game_manager::save_game(const char* buf, uint32_t len) {
}

void __fastcall c_game_manager::set_game_result(s_game_result* result) {
}

void __fastcall c_game_manager::pause_game(int a1) {

}

void __fastcall c_game_manager::_pause_game_(int a1) {

}

void __fastcall c_game_manager::set_game_objectives(
	const wchar_t* primary, 
	const wchar_t* secondary) {

}

void __fastcall c_game_manager::set_game_engine_variant(i_game_engine_variant* variant) {
}

void __fastcall c_game_manager::set_scenario_map_variant(i_scenario_map_variant* variant) {
}

uintptr_t __fastcall libmcc::c_game_manager::get_mcc_string(const char* string_name, wchar_t* buf, size_t len) {
	return uintptr_t();
}

bool __fastcall libmcc::c_game_manager::use_custom_string_mapping() {
	return false;
}

void __fastcall libmcc::c_game_manager::insert_string(int unic_datum_index, int string_id, const char* unic_tag_name, const char* string_name) {
}

bool __fastcall libmcc::c_game_manager::get_string(int unic_datum_index, int string_id, wchar_t* buf, size_t len) {
	return false;
}

bool __fastcall c_game_manager::get_subtitle(
	const char* sound_tag_name, 
	const char* prefix, 
	int index, 
	wchar_t* buf, 
	size_t len
) {
	return false;
}
