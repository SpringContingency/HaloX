#include "halox_audio.h"

#include "mcc_user_settings.h"
#include "../logging/logging.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace halox::audio {

namespace {

// Miles AIL bindings — resolved lazily out of mss64.dll once it has been
// loaded into our process by the game's audio init. We don't try to load it
// ourselves: forcing mss64 to map before halo2 has wired its config would
// initialize a Miles environment the game wasn't expecting and would
// confuse the engine's later AIL_open_digital_driver call.
struct miles_bindings {
	bool                resolved        = false;     // last attempt succeeded
	void*               primary_driver  = nullptr;   // cached HDIGDRIVER
	using fn_primary_t  = void* (__stdcall*)();
	using fn_set_vol_t  = void  (__stdcall*)(void* driver, float level);
	fn_primary_t        primary         = nullptr;
	fn_set_vol_t        set_vol         = nullptr;
};

static miles_bindings g_miles;

// Resolve once mss64.dll is loaded by the game. Returns true if usable.
bool resolve_miles() {
	if (g_miles.resolved && g_miles.primary && g_miles.set_vol) return true;

	HMODULE mss = GetModuleHandleW(L"mss64.dll");
	if (!mss) {
		// Halo2 hasn't initialized audio yet (or the dll lives under a
		// non-default name; halo3 and earlier games sometimes lazy-load).
		return false;
	}
	auto p = (miles_bindings::fn_primary_t)
		GetProcAddress(mss, "AIL_primary_digital_driver");
	auto s = (miles_bindings::fn_set_vol_t)
		GetProcAddress(mss, "AIL_set_digital_master_volume_level");
	if (!p || !s) {
		CONSOLE_LOG_WARN("halox audio: mss64.dll loaded but exports missing "
			"(primary=%p set=%p)", p, s);
		return false;
	}
	g_miles.primary  = p;
	g_miles.set_vol  = s;
	g_miles.resolved = true;
	return true;
}

bool miles_set_master_pct(int pct) {
	if (!resolve_miles()) return false;
	if (!g_miles.primary_driver) {
		g_miles.primary_driver = g_miles.primary();
		if (!g_miles.primary_driver) {
			// Driver not opened yet — game is still booting audio.
			return false;
		}
	}
	float level = (float)(pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100.0f;
	g_miles.set_vol(g_miles.primary_driver, level);
	return true;
}

bool is_miles_game(libmcc::e_module module) {
	return module == libmcc::_module_halo1 || module == libmcc::_module_halo2;
}

}  // namespace

bool apply_audio_for_module(libmcc::e_module module) {
	if (module < 0 || module >= libmcc::k_game_count) return false;
	const auto* s = mcc_user_settings();
	if (!s) return false;
	const auto& a = s->audio[module];
	int master = a.muted ? 0 : a.master_volume;

	if (is_miles_game(module)) {
		// halo2/halo1 only have a master knob wired right now. SFX/music/voice
		// go through the same digital driver and would need bus-level RE.
		return miles_set_master_pct(master);
	}
	// Wwise titles — not implemented yet. Returning false keeps callers honest:
	// they know the volume didn't take effect, and the UI can grey out the
	// audio sliders for these games.
	return false;
}

bool set_master_volume_pct(libmcc::e_module module, int pct) {
	if (module < 0 || module >= libmcc::k_game_count) return false;
	auto* s = mcc_user_settings_mut();
	if (!s) return false;
	if (pct < 0)   pct = 0;
	if (pct > 100) pct = 100;
	s->audio[module].master_volume = pct;
	return apply_audio_for_module(module);
}

}  // namespace halox::audio
