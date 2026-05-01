#include "ui_progress.h"

#include "../diag/launch_liveness.h"
#include "../game/game_instance_manager.h"
#include "../logging/logging.h"
#include "../main/main.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace libmcc;

namespace halox::ui {

namespace {

// Page-readable test, prevents AVs when the struct hasn't been initialized
// yet (e.g. very early in launch before the game DLL constructs its loading
// system).
bool readable(const void* p, size_t n) {
	if (!p) return false;
	MEMORY_BASIC_INFORMATION mbi{};
	if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
	if (mbi.State != MEM_COMMIT) return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
	const auto base = (uintptr_t)p;
	const auto end  = base + n;
	const auto rend = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
	return end <= rend;
}

constexpr uintptr_t k_h2_progress_struct_rva = 0x1E90E70;
constexpr uintptr_t k_h2_progress_active_off = 0x80;
constexpr uintptr_t k_h2_progress_value_off  = 0xAC;

constexpr uintptr_t k_reach_state_rva  = 0xC1A114;
constexpr uintptr_t k_reach_substate_rva = 0xC1A118;

const char* reach_state_name(uint32_t s) {
	switch (s) {
	case 0:  return "Initializing";
	case 1:  return "Session pending";
	case 2:  return "Joining session";
	case 3:  return "Loading film";
	case 4:  return "Playback";
	case 5:  return "Finalizing";
	case 6:  return "Session tick";
	case 7:  return "Session tick";
	case 8:  return "Session tick";
	case 9:  return "Session join update";
	case 10: return "Session network tick";
	case 11: return "Loading map";
	default: return "Working";
	}
}

}  // namespace

// Wall-clock at game-thread-started moment. 0 = no game.
static std::atomic<uint64_t> s_game_thread_start_ms{0};
// Wall-clock when liveness first reported stable_alive. 0 = not yet.
static std::atomic<uint64_t> s_stable_alive_at_ms{0};
// Effective displayed fraction at the instant stable_alive triggered —
// the finalize extension ramps from this value up to 1.0 across the
// k_finalize_extend_ms window so the bar reaches 100% precisely as the
// loading screen releases.
static std::atomic<uint32_t> s_value_at_stable_x1000{0};

// Extension window — engine has reported "playing" via stable_alive but
// we keep the loading UI visible this much longer to mask the post-load
// black-frame swap. The bar fills smoothly to 100% over this window.
// The game itself is not blocked during this period; only the overlay
// is held.
static constexpr uint64_t k_finalize_extend_ms = 5000;

// Pre-stable cap on the displayed fraction. Engine's reported value plus
// the time-based synthetic estimate are both clamped to this so the 2s
// finalize ramp has room to do meaningful work (cap → 1.0).
static constexpr float    k_pre_stable_cap = 0.85f;

// Engine reports 0.0 → 0.35 → 0.60 in a couple seconds then goes silent
// for ~6-10s while map data streams. The synthetic estimate fills that
// gap. Tuned for halo2 campaign loads.
static constexpr uint64_t k_estimated_load_ms = 8000;

// Compute the time-based effective fraction (synthetic ramp + finalize
// override). Returned in a fully-initialized s_progress with valid=true.
// Reused by both the generic-no-module path and the halo2-fallback path
// (when halo2's progress struct hasn't been allocated yet).
static s_progress build_synthetic_progress() {
	s_progress out{};
	out.valid    = true;
	out.fraction = 0.0f;
	out.step[0]  = 0;

	uint64_t start = s_game_thread_start_ms.load(std::memory_order_acquire);
	uint64_t now = GetTickCount64();
	uint64_t fa = s_stable_alive_at_ms.load(std::memory_order_acquire);
	float effective;
	if (fa && now >= fa) {
		float t = (float)(now - fa) / (float)k_finalize_extend_ms;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		float v_at = (float)s_value_at_stable_x1000.load(
			std::memory_order_acquire) / 1000.0f;
		if (v_at <= 0.0f) v_at = k_pre_stable_cap;
		effective = v_at + (1.0f - v_at) * t;
	} else {
		float synthetic = 0.0f;
		if (start && now > start) {
			synthetic = (float)(now - start) / (float)k_estimated_load_ms;
			synthetic *= k_pre_stable_cap;
		}
		if (synthetic > k_pre_stable_cap) synthetic = k_pre_stable_cap;
		effective = synthetic;
	}
	out.fraction = effective;
	int pct = (int)(effective * 100.0f);
	if (pct > 100) pct = 100;
	std::snprintf(out.step, sizeof(out.step), "Loading  %d%%", pct);
	return out;
}

s_progress progress_query() {
	s_progress out{};
	out.valid    = false;
	out.fraction = -1.0f;
	out.step[0]  = 0;

	auto* gim = game_instance_manager();
	if (!gim) return out;
	auto game = gim->get_game();

	// Generic synthetic-only path — used when the launch worker is
	// running but the game module isn't halo2 (or hasn't been set yet,
	// because m_game is populated mid-way through launch_game_internal).
	// Without this the loading screen stays as an indeterminate spinner
	// for ~1–2s after PLAY before transitioning to a real percent.
	if (game != _module_halo2 && s_game_thread_start_ms.load() != 0) {
		return build_synthetic_progress();
	}

	if (game == _module_halo2) {
		HMODULE h = GetModuleHandleW(L"halo2.dll");
		if (!h) return build_synthetic_progress();
		auto base = (uintptr_t)h;
		// DAT_181E90E70 is a pointer to a heap-allocated progress struct,
		// NOT the struct itself. The struct is created by halo2 partway
		// through the load sequence, so the pointer reads NULL until then.
		// We fall back to the synthetic time-based ramp for that window
		// so the bar keeps moving instead of disappearing.
		uintptr_t* p_struct_ptr =
			reinterpret_cast<uintptr_t*>(base + k_h2_progress_struct_rva);
		if (!readable(p_struct_ptr, 8)) {
			return build_synthetic_progress();
		}
		uintptr_t struct_addr = *p_struct_ptr;
		if (!struct_addr) {
			static int s_log_null = 0;
			if (s_log_null < 4) {
				++s_log_null;
				CONSOLE_LOG_INFO("progress: halo2 ptr slot @ %p is NULL — using synthetic ramp",
					(void*)p_struct_ptr);
			}
			return build_synthetic_progress();
		}

		uint8_t* active = reinterpret_cast<uint8_t*>(struct_addr + k_h2_progress_active_off);
		float*   pval   = reinterpret_cast<float*>(struct_addr + k_h2_progress_value_off);
		if (!readable(active, 1) || !readable(pval, 4)) {
			return build_synthetic_progress();
		}
		float v = *pval;
		uint8_t a = *active;

		// Diagnostic: log first few + any 5% advancement so we can verify
		// the indirect-deref address is correct.
		static int   s_log_count = 0;
		static float s_last_logged = -1.0f;
		bool log_now = (s_log_count < 6) ||
			(s_last_logged < 0.0f) ||
			(v >= s_last_logged + 0.05f);
		if (log_now) {
			++s_log_count;
			s_last_logged = v;
			uint32_t raw = 0;
			std::memcpy(&raw, &v, 4);
			CONSOLE_LOG_INFO("progress: halo2 read struct=%p active=0x%02X frac=%.4f raw=0x%08X",
				(void*)struct_addr, (unsigned)a, v, raw);
		}

		(void)a;
		if (!(v >= 0.0f && v <= 1.0f)) return build_synthetic_progress();
		return build_synthetic_progress();
	}

	if (game == _module_haloreach) {
		HMODULE h = GetModuleHandleW(L"haloreach.dll");
		if (!h) return out;
		auto base = (uintptr_t)h;
		uint32_t* pstate = reinterpret_cast<uint32_t*>(base + k_reach_state_rva);
		uint32_t* psub   = reinterpret_cast<uint32_t*>(base + k_reach_substate_rva);
		if (!readable(pstate, 4) || !readable(psub, 4)) return out;
		uint32_t s = *pstate;
		uint32_t ss = *psub;
		out.valid    = true;
		out.fraction = -1.0f;
		std::snprintf(out.step, sizeof(out.step), "%s  (state %u.%u)",
			reach_state_name(s), s, ss);
		return out;
	}

	return out;
}

uint64_t progress_finalize_started_ms() {
	return s_stable_alive_at_ms.load(std::memory_order_acquire);
}

uint64_t progress_launch_epoch() {
	return s_game_thread_start_ms.load(std::memory_order_acquire);
}

void progress_mark_game_thread_started() {
	s_game_thread_start_ms.store(GetTickCount64(), std::memory_order_release);
	s_stable_alive_at_ms.store(0, std::memory_order_release);
	s_value_at_stable_x1000.store(0, std::memory_order_release);
}

bool progress_is_loading() {
	auto* gim = game_instance_manager();
	if (!gim) return false;
	// Game thread not yet alive — caller already shows the launch screen
	// based on g_launch_in_progress. Reporting false here is fine.
	if (!gim->in_game()) {
		s_game_thread_start_ms.store(0, std::memory_order_release);
		s_stable_alive_at_ms.store(0, std::memory_order_release);
		return false;
	}

	uint64_t start = s_game_thread_start_ms.load(std::memory_order_acquire);
	uint64_t now = GetTickCount64();
	uint64_t elapsed_ms = (start && now > start) ? (now - start) : 0;

	// One-shot latch: once we already passed the finalize window for this
	// launch, the load is definitively complete. Don't re-enter loading
	// state on later frames even if the screen goes black again (pause-
	// menu dim, cutscene transition, alt-tab) — that's not a re-load.
	{
		uint64_t fa = s_stable_alive_at_ms.load(std::memory_order_acquire);
		if (fa && now > fa && (now - fa) >= k_finalize_extend_ms) {
			return false;
		}
	}

	// Stable_alive means the engine is rendering real frames for 3 consecutive
	// 1s samples, so it lags FIRST_ALIVE by ~3s. We use stable for campaign
	// (filters out cutscene black-frame flicker), then optionally extend by
	// k_finalize_extend_ms to mask the post-load black-frame swap.
	//
	// EXEMPT multiplayer: MP has no cutscene flicker and no post-load black-
	// frame artifact — the lobby/spawn frame is the real game and the user
	// sees the load screen as a pure "still loading?" gate. Use first-alive
	// (no 3s consecutive-sample wait, no finalize extension) so the load
	// screen drops the moment MP starts rendering anything.
	const bool is_campaign = (gim->get_mode() == _game_mode_campaign);
	if (!is_campaign) {
		if (liveness_first_alive_seen()) {
			return false;
		}
		// Fall through to the no-alive-yet timeout cap below.
		auto game = gim->get_game();
		const uint64_t cap_ms =
			(game == _module_halo2 || game == _module_haloreach) ? 90000 : 30000;
		return elapsed_ms < cap_ms;
	}

	// Campaign path: require stable (filters cutscene flicker) and only halo2/
	// reach need the finalize extension to mask their post-load black-frame.
	const bool stable = liveness_stable_alive();
	const bool needs_finalize_extend =
		gim->get_game() == _module_halo2 || gim->get_game() == _module_haloreach;
	if (stable && !needs_finalize_extend) {
		return false;
	}
	if (stable) {
		uint64_t fa = s_stable_alive_at_ms.load(std::memory_order_acquire);
		if (fa == 0) {
			// Capture the displayed-fraction snapshot at the transition
			// moment so the finalize ramp lerps from a stable value
			// (otherwise the synthetic estimate would keep advancing
			// during the extension and warp the ramp).
			s_stable_alive_at_ms.store(now, std::memory_order_release);
			uint64_t start = s_game_thread_start_ms.load(std::memory_order_acquire);
			float pre = 0.0f;
			if (start && now > start) {
				pre = (float)(now - start) / (float)k_estimated_load_ms;
				pre *= k_pre_stable_cap;
				if (pre > k_pre_stable_cap) pre = k_pre_stable_cap;
			}
			s_value_at_stable_x1000.store(
				(uint32_t)(pre * 1000.0f + 0.5f),
				std::memory_order_release);
			fa = now;
		}
		if (now > fa && (now - fa) >= k_finalize_extend_ms) {
			return false;
		}
		return true;
	}

	// No alive frame yet. Cap so a black-screen freeze can't trap us.
	auto game = gim->get_game();
	const uint64_t cap_ms =
		(game == _module_halo2 || game == _module_haloreach) ? 90000 : 30000;
	return elapsed_ms < cap_ms;
}

}  // namespace halox::ui
