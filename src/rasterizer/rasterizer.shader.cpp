#include "rasterizer.h"

#include <d3dcompiler.h>

constexpr const char* k_simple_pixel_shader = R"(
Texture2D texture0 : register(t0);
SamplerState sampler0 : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = input.col * texture0.Sample(sampler0, input.uv);
    color.a = 1.0f;
    return color;
}
)";

int c_rasterizer::initialize_shader() {
	for (int i = 0; i < k_shader_count; ++i) {
		auto shader = m_shaders + i;
		switch (i) {
		case _shader_simple: {
			ID3DBlob* pixel_shader_blob = nullptr;
			ID3DBlob* error_blob = nullptr;

			HRESULT hr = D3DCompile(
				k_simple_pixel_shader,
				strlen(k_simple_pixel_shader),
				nullptr,
				nullptr,
				nullptr,
				"main",
				"ps_5_0",
				0,
				0,
				&pixel_shader_blob,
				&error_blob
			);

			if (FAILED(hr)) {
				if (error_blob) {
					OutputDebugStringA((char*)error_blob->GetBufferPointer());
					error_blob->Release();
				}
				return 1;
			}

			hr = m_d3d11_device->CreatePixelShader(
				pixel_shader_blob->GetBufferPointer(),
				pixel_shader_blob->GetBufferSize(),
				nullptr,
				&shader->pixel_shader
			);

			pixel_shader_blob->Release();

			break;
		}
		default:
			break;
		}
	}

	return 0;
}

int c_rasterizer::destory_shader() {
	for (int i = 0; i < k_shader_count; ++i) {
		auto shader = m_shaders + i;

		if (shader->constant_buffer) {
			shader->constant_buffer->Release();
		}

		if (shader->input_layout) {
			shader->input_layout->Release();
		}

		if (shader->pixel_shader) {
			shader->pixel_shader->Release();
		}

		if (shader->vertex_shader) {
			shader->vertex_shader->Release();
		}
	}

	return 0;
}

int c_rasterizer::set_shader(e_shader shader) {
	auto _shader = m_shaders + shader;

	if (_shader->pixel_shader) {
		m_d3d11_device_context->PSSetShader(
			_shader->pixel_shader,
			nullptr,
			0
		);
	}

	if (_shader->vertex_shader) {
		m_d3d11_device_context->VSSetShader(
			_shader->vertex_shader,
			nullptr,
			0
		);
	}

	return 0;
}
