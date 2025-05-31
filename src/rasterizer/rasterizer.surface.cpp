#include "rasterizer.h"

int c_rasterizer::initialize_surface() {
	if (!m_d3d11_device || !m_dxgi_swap_chain) {
		return 1;
	}

	for (int i = 0; i < k_surface_count; ++i) {
		auto surface = m_surfaces + i;

		switch (i) {
		case _surface_back_buffer: {
			m_dxgi_swap_chain->GetBuffer(
				0,
				IID_PPV_ARGS(&surface->texture));
			m_d3d11_device->CreateRenderTargetView(
				surface->texture,
				nullptr,
				&surface->target);
			break;
		}
		case _surface_game: {
			ID3D11Texture2D* back_buffer;
			D3D11_TEXTURE2D_DESC back_buffer_desc;

			m_dxgi_swap_chain->GetBuffer(
				0,
				IID_PPV_ARGS(&back_buffer));

			back_buffer->GetDesc(&back_buffer_desc);

			D3D11_TEXTURE2D_DESC desc {
				.Width = back_buffer_desc.Width,
				.Height = back_buffer_desc.Height,
				.MipLevels = 1,
				.ArraySize = 1,
				.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
				.SampleDesc = {1, 0},
				.Usage = D3D11_USAGE_DEFAULT,
				.BindFlags = 
					D3D11_BIND_SHADER_RESOURCE |
					D3D11_BIND_RENDER_TARGET,
				.CPUAccessFlags = 0,
				.MiscFlags = 0
			};

			back_buffer->Release();

			m_d3d11_device->CreateTexture2D(
				&desc,
				nullptr,
				&surface->texture);

			m_d3d11_device->CreateRenderTargetView(
				surface->texture, 
				nullptr, 
				&surface->target);

			m_d3d11_device->CreateShaderResourceView(
				surface->texture,
				nullptr,
				&surface->resource);
			break;
		}
		default:
			break;
		}
	}
	return 0;
}

int c_rasterizer::destory_surface() {
	if (!m_d3d11_device || !m_dxgi_swap_chain) {
		return 1;
	}

	for (int i = 0; i < k_surface_count; ++i) {
		auto surface = m_surfaces + i;
		switch (i) {
		default:
			if (surface->resource) {
				surface->resource->Release();
			}

			if (surface->target) {
				surface->target->Release();
			}

			if (surface->texture) {
				surface->texture->Release();
			}

			break;
		}
	}
	return 0;
}

int c_rasterizer::set_surface(e_surface surface) {
	m_d3d11_device_context->OMSetRenderTargets(
		1, 
		&m_surfaces[surface].target, 
		nullptr);
	return 0;
}

int c_rasterizer::clear_surface(e_surface surface, const float* rgba) {
	m_d3d11_device_context->ClearRenderTargetView(
		m_surfaces[surface].target,
		rgba);
	return 0;
}

int c_rasterizer::copy_surface(e_surface dst, e_surface src) {
	m_d3d11_device_context->CopyResource(
		m_surfaces[dst].texture,
		m_surfaces[src].texture);
	return 0;
}
