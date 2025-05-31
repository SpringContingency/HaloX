#include "rasterizer.h"

int c_rasterizer::create_sprite(int width, int height) {
	s_sprite local_sprite;
	D3D11_TEXTURE2D_DESC desc {
		.Width = (UINT)width,
		.Height = (UINT)height,
		.MipLevels = 1,
		.ArraySize = 1,
		.Format = DXGI_FORMAT_A8_UNORM,
		.SampleDesc = {1, 0},
		.Usage = D3D11_USAGE_DEFAULT,
		.BindFlags = D3D11_BIND_SHADER_RESOURCE,
		.CPUAccessFlags = 0,
		.MiscFlags = 0
	};

	m_d3d11_device->CreateTexture2D(
		&desc,
		nullptr,
		&local_sprite.texture);

	m_d3d11_device->CreateShaderResourceView(
		local_sprite.texture,
		nullptr,
		&local_sprite.resource);

	for (int i = 0; i < m_sprites.size(); ++i) {
		auto& sprite = m_sprites[i];

		if (sprite.texture != nullptr) {
			continue;
		}

		sprite.texture = local_sprite.texture;
		sprite.resource = local_sprite.resource;

		return i;
	}

	m_sprites.push_back(local_sprite);

	return m_sprites.size() - 1;
}

int c_rasterizer::destory_sprite(int index) {
	if (index < 0 || index >= m_sprites.size()) {
		return 1;
	}

	auto& sprite = m_sprites[index];

	if (sprite.texture) {
		sprite.texture->Release();
		sprite.texture = nullptr;
	}

	if (sprite.resource) {
		sprite.resource->Release();
		sprite.resource = nullptr;
	}

	return 0;
}

int c_rasterizer::destory_sprites() {
	for (auto& sprite : m_sprites) {
		if (sprite.texture) {
			sprite.texture->Release();
			sprite.texture = nullptr;
		}
		if (sprite.resource) {
			sprite.resource->Release();
			sprite.resource = nullptr;
		}
	}

	m_sprites.clear();

	return 0;
}
