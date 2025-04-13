#include "rasterizer.h"

#include <array>
#include "../logging/logging.h"

int c_rasterizer::initialize_device() {
	UINT flags;
	HRESULT hr;
	D3D_FEATURE_LEVEL feature_level;

	if (m_dxgi_swap_chain != nullptr) {
		return 0;
	}

	if (g_win32_parameter.window_handle == NULL) {
		return 1;
	}

	flags = 0
#ifdef _DEBUG
		| D3D11_CREATE_DEVICE_DEBUG
#endif
		;

	constexpr std::array<D3D_FEATURE_LEVEL, 2> d3d_feature_levels{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_0
	};

	hr = D3D11CreateDevice(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		flags,
		d3d_feature_levels.data(),
		d3d_feature_levels.size(),
		D3D11_SDK_VERSION,
		&m_d3d11_device,
		&feature_level,
		&m_d3d11_device_context
	);

	if (FAILED(hr)) {
		return 2;
	}

	IDXGIDevice* pDXGIDevice = nullptr;
	IDXGIAdapter* pDXGIAdapter = nullptr;

	if (SUCCEEDED(m_d3d11_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&pDXGIDevice))))
		if (SUCCEEDED(pDXGIDevice->GetAdapter(&pDXGIAdapter)))
			pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&m_dxgi_factory));
	if (pDXGIDevice) pDXGIDevice->Release();
	if (pDXGIAdapter) pDXGIAdapter->Release();

	if (m_dxgi_factory == nullptr) {
		shutdown_device();
		return 3;
	}

	RECT rect;

	GetClientRect(g_win32_parameter.window_handle, &rect);

	DXGI_SWAP_CHAIN_DESC dxgi_swap_chain_desc = {
		.BufferDesc = {
			.Width = static_cast<UINT>(rect.right - rect.left),
			.Height = static_cast<UINT>(rect.bottom - rect.top),
			.RefreshRate = {
				.Numerator = 60,
				.Denominator = 1
			},
			.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
			.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
			.Scaling = DXGI_MODE_SCALING_UNSPECIFIED,
		},
		.SampleDesc = {
			.Count = 1,
			.Quality = 0
		},
		.BufferUsage = 
			DXGI_USAGE_RENDER_TARGET_OUTPUT | 
			DXGI_USAGE_UNORDERED_ACCESS | 
			DXGI_USAGE_SHADER_INPUT,
		.BufferCount = 2,
		.OutputWindow = g_win32_parameter.window_handle,
		.Windowed = TRUE,
		.SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
		.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
	};

	hr = m_dxgi_factory->CreateSwapChain(
		m_d3d11_device, 
		&dxgi_swap_chain_desc, 
		&m_dxgi_swap_chain);

	if (FAILED(hr)) {
		shutdown_device();
		return 4;
	}

	return 0;
}

int c_rasterizer::shutdown_device() {
	if (m_dxgi_swap_chain) {
		m_dxgi_swap_chain->Release();
		m_dxgi_swap_chain = nullptr;
	}

	if (m_dxgi_factory) {
		m_dxgi_factory->Release();
		m_dxgi_factory = nullptr;
	}

	if (m_d3d11_device_context) {
		m_d3d11_device_context->Release();
		m_d3d11_device_context = nullptr;
	}

	if (m_d3d11_device) {
		m_d3d11_device->Release();
		m_d3d11_device = nullptr;
	}

	return 0;
}

int c_rasterizer::begin_frame_device() {
	if (g_win32_parameter.window_thread == GetCurrentThreadId()) {
		clear_surface(_surface_back_buffer, k_color_black);
	}
	return 0;
}

int c_rasterizer::end_frame_device() {
	if (g_win32_parameter.window_thread == GetCurrentThreadId()) {
		m_dxgi_swap_chain->Present(1, 0);
	}
	return 0;
}

int c_rasterizer::resize_device(int width, int height) {
	HRESULT result;

	if (!m_dxgi_swap_chain) {
		return 1;
	}

	result = m_dxgi_swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

	if (FAILED(result)) {
		CONSOLE_LOG_ERROR("Failed to resize swap chain buffers");
		return 2;
	}

	return 0;
}
