#include "launch_liveness.h"

#include "../game/game_instance_manager.h"
#include "../logging/logging.h"
#include "../rasterizer/rasterizer.h"

#include <Windows.h>
#include <d3d11.h>
#include <cstdint>

using namespace libmcc;

// Throttle: sample at most once per second to keep cost negligible.
static const double k_sample_interval_s = 1.0;

// "All black" threshold: per-channel value below which we count a pixel as
// black. With float-to-byte rounding, the game's actual clear-to-black
// produces 0,0,0 — but blooming render targets sometimes leave 1..2 noise.
static const uint8_t k_black_channel_threshold = 4;

// How long a solid-black streak must be before we escalate to a WARN.
static const double k_black_warn_after_s = 5.0;

// Cached staging texture sized to the current game surface. Recreated on
// resolution change.
static ID3D11Texture2D* g_staging      = nullptr;
static UINT             g_staging_w    = 0;
static UINT             g_staging_h    = 0;

// Wall-clock timestamps (LARGE_INTEGER QPC) for throttling and streak tracking.
static LARGE_INTEGER    g_qpc_freq         = {};
static LARGE_INTEGER    g_qpc_last_sample  = {};
static LARGE_INTEGER    g_qpc_in_game_at   = {};
static LARGE_INTEGER    g_qpc_first_alive  = {};
static LARGE_INTEGER    g_qpc_first_black  = {};
static bool             g_was_in_game      = false;
static bool             g_ever_alive       = false;
static bool             g_warned_black     = false;
static int              g_consecutive_black_samples = 0;
static int              g_consecutive_alive_samples = 0;

// How many consecutive alive samples constitute "stable alive" — i.e. the
// engine has finished the load splash and is steadily rendering frames.
// Sample rate is ~1Hz, so 3 = ~3s of solid alive frames.
static const int        k_stable_alive_threshold = 3;

static double qpc_seconds_since(const LARGE_INTEGER& start) {
	if (g_qpc_freq.QuadPart == 0) {
		QueryPerformanceFrequency(&g_qpc_freq);
		if (g_qpc_freq.QuadPart == 0) return 0.0;
	}
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - start.QuadPart) / (double)g_qpc_freq.QuadPart;
}

static void release_staging() {
	if (g_staging) { g_staging->Release(); g_staging = nullptr; }
	g_staging_w = g_staging_h = 0;
}

static bool ensure_staging(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT fmt) {
	if (g_staging && g_staging_w == w && g_staging_h == h) return true;
	release_staging();
	D3D11_TEXTURE2D_DESC d{};
	d.Width            = w;
	d.Height           = h;
	d.MipLevels        = 1;
	d.ArraySize        = 1;
	d.Format           = fmt;
	d.SampleDesc.Count = 1;
	d.Usage            = D3D11_USAGE_STAGING;
	d.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
	HRESULT hr = dev->CreateTexture2D(&d, nullptr, &g_staging);
	if (FAILED(hr) || !g_staging) {
		CONSOLE_LOG_WARN("liveness: CreateTexture2D(staging %ux%u fmt=%d) failed hr=0x%08lX",
			w, h, (int)fmt, (unsigned long)hr);
		return false;
	}
	g_staging_w = w;
	g_staging_h = h;
	return true;
}

bool liveness_first_alive_seen() {
	return g_ever_alive;
}

bool liveness_stable_alive() {
	return g_consecutive_alive_samples >= k_stable_alive_threshold;
}

void liveness_probe_tick() {
	const bool in_game = game_instance_manager()->in_game();

	// On first in_game tick, capture the start time so we can attribute "Ns
	// after launch" in log messages.
	if (in_game && !g_was_in_game) {
		QueryPerformanceCounter(&g_qpc_in_game_at);
		g_qpc_last_sample.QuadPart = 0;
		g_qpc_first_alive.QuadPart = 0;
		g_qpc_first_black.QuadPart = 0;
		g_ever_alive               = false;
		g_warned_black             = false;
		g_consecutive_black_samples = 0;
		g_consecutive_alive_samples = 0;
		CONSOLE_LOG_INFO("liveness: in_game tick stream begins (probe armed)");
	}
	if (!in_game && g_was_in_game) {
		release_staging();
		CONSOLE_LOG_INFO("liveness: in_game tick stream ended (ever_alive=%d)", (int)g_ever_alive);
	}
	g_was_in_game = in_game;
	if (!in_game) return;

	// Throttle to once per ~1s.
	if (g_qpc_last_sample.QuadPart != 0) {
		double dt = qpc_seconds_since(g_qpc_last_sample);
		if (dt < k_sample_interval_s) return;
	}
	QueryPerformanceCounter(&g_qpc_last_sample);

	auto* r = rasterizer();
	auto* dev = r->get_device();
	auto* ctx = r->get_device_context();
	auto* surf = r->get_surface(_surface_game);
	if (!dev || !ctx || !surf || !surf->texture) return;

	D3D11_TEXTURE2D_DESC desc{};
	surf->texture->GetDesc(&desc);
	if (desc.Width == 0 || desc.Height == 0) return;

	if (!ensure_staging(dev, desc.Width, desc.Height, desc.Format)) return;

	// Non-blocking GPU readback, two-tick deferred:
	//   Tick N:   issue CopyResource → set pending flag → return
	//   Tick N+1: try Map(DO_NOT_WAIT) → with ~1s of GPU time, the copy is
	//             reliably done; on the rare WAS_STILL_DRAWING just retry
	//             next tick. After consuming, issue the next CopyResource so
	//             the staging stays one tick ahead.
	// Blocking Map(READ) here flushed the entire pipeline and caused a 1Hz
	// stutter under heavy renderers (halo2 Anniversary). The deferred path
	// keeps the readback non-blocking AND reliable.
	static bool s_copy_pending = false;
	if (!s_copy_pending) {
		ctx->CopyResource(g_staging, surf->texture);
		s_copy_pending = true;
		return;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = ctx->Map(g_staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
		return;  // copy still draining; keep s_copy_pending=true, retry next tick
	}
	if (FAILED(hr) || !mapped.pData) {
		s_copy_pending = false;
		return;
	}

	// Sample 9 pixels: corners, midpoints of each edge, and center.
	const UINT w = desc.Width, h = desc.Height;
	const UINT xs[3] = { 4u, w / 2u, (w >= 5u) ? w - 5u : w / 2u };
	const UINT ys[3] = { 4u, h / 2u, (h >= 5u) ? h - 5u : h / 2u };

	uint32_t samples[9] = {};
	int alive_count = 0;
	int n = 0;
	for (int j = 0; j < 3; ++j) {
		for (int i = 0; i < 3; ++i) {
			UINT x = xs[i], y = ys[j];
			if (x >= w) x = w - 1;
			if (y >= h) y = h - 1;
			const uint8_t* row = (const uint8_t*)mapped.pData + (size_t)mapped.RowPitch * y;
			// Most likely formats here are R8G8B8A8_UNORM or B8G8R8A8_UNORM —
			// both have 4 bytes per pixel and the channel order doesn't matter
			// for "is black" detection.
			const uint8_t* px = row + x * 4;
			const uint8_t r0 = px[0], g0 = px[1], b0 = px[2];
			samples[n] = ((uint32_t)r0 << 16) | ((uint32_t)g0 << 8) | (uint32_t)b0;
			if (r0 > k_black_channel_threshold ||
			    g0 > k_black_channel_threshold ||
			    b0 > k_black_channel_threshold) {
				++alive_count;
			}
			++n;
		}
	}
	ctx->Unmap(g_staging, 0);

	// Consumed this tick's copy; queue the next one so we stay one tick ahead.
	ctx->CopyResource(g_staging, surf->texture);
	// s_copy_pending stays true — the copy we just issued will be ready next tick.

	const double t_in_game = qpc_seconds_since(g_qpc_in_game_at);
	const bool   alive     = alive_count > 0;

	if (alive) {
		if (!g_ever_alive) {
			QueryPerformanceCounter(&g_qpc_first_alive);
			g_ever_alive = true;
			CONSOLE_LOG_INFO("liveness: FIRST ALIVE FRAME at %.1fs in_game (alive_pixels=%d/9, samples 0x%06X 0x%06X 0x%06X)",
				t_in_game, alive_count, samples[0], samples[4], samples[8]);
		} else {
			CONSOLE_LOG_INFO("liveness: alive (t=%.1fs, alive_pixels=%d/9, center=0x%06X)",
				t_in_game, alive_count, samples[4]);
		}
		g_consecutive_black_samples = 0;
		g_qpc_first_black.QuadPart  = 0;
		g_warned_black              = false;
		++g_consecutive_alive_samples;
		return;
	}

	// All sampled pixels are black.
	g_consecutive_alive_samples = 0;
	if (g_consecutive_black_samples == 0) {
		QueryPerformanceCounter(&g_qpc_first_black);
	}
	++g_consecutive_black_samples;

	const double black_streak_s = qpc_seconds_since(g_qpc_first_black);
	CONSOLE_LOG_INFO("liveness: BLACK (t=%.1fs in_game, streak=%.1fs, samples %d)",
		t_in_game, black_streak_s, g_consecutive_black_samples);

	if (!g_warned_black && black_streak_s >= k_black_warn_after_s) {
		g_warned_black = true;
		CONSOLE_LOG_WARN("liveness: surface has been solid black for %.1fs in_game — "
		                "game appears stuck/black-screened (process may be alive but not rendering)",
			black_streak_s);
	}
}
