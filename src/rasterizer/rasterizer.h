#pragma once

#include <vector>
#include "../main/main.h"

constexpr FLOAT k_color_black[4] = { 0.f, 0.f, 0.f, 1.f };

enum e_shader {
	_shader_simple,

	k_shader_count
};

enum e_surface {
	_surface_back_buffer,
	_surface_game,

	k_surface_count
};

struct s_shader {
	ID3D11VertexShader* vertex_shader;
	ID3D11PixelShader* pixel_shader;
	ID3D11InputLayout* input_layout;
	ID3D11Buffer* constant_buffer;
};

struct s_sprite {
	ID3D11Texture2D* texture;
	ID3D11ShaderResourceView* resource;
};

struct s_surface {
	ID3D11Texture2D* texture;
	ID3D11RenderTargetView* target;
	ID3D11ShaderResourceView* resource;
};

class c_rasterizer {
public:
	int initialize();
	int shutdown();

	int begin_frame();
	int end_frame();

	int resize(int width, int height);

	int set_surface(e_surface surface);
	int clear_surface(e_surface surface, const float* rgba);
	int copy_surface(e_surface dst, e_surface src);
	inline const s_surface* get_surface(e_surface surface) {return &m_surfaces[surface];}

	int create_sprite(int width, int height);
	int destory_sprite(int index);
	inline const s_sprite* get_sprite(int index) { return &m_sprites[index]; }

	int set_shader(e_shader shader);

	inline ID3D11Device* get_device() {return m_d3d11_device;}
	inline ID3D11DeviceContext* get_device_context() {return m_d3d11_device_context;}
	inline IDXGISwapChain* get_swap_chain() {return m_dxgi_swap_chain;}

	static c_rasterizer g_rasterizer;
private:
	int initialize_device();
	int shutdown_device();
	int begin_frame_device();
	int end_frame_device();
	int resize_device(int width, int height);

	int initialize_window();
	int shutdown_window();

	int initialize_shader();
	int destory_shader();

	int initialize_imgui();
	int shutdown_imgui();
	int begin_frame_imgui();
	int end_frame_imgui();
	
	int initialize_surface();
	int destory_surface();

	int destory_sprites();

private:
	IDXGIFactory* m_dxgi_factory;
	ID3D11Device* m_d3d11_device;
	ID3D11DeviceContext* m_d3d11_device_context;
	IDXGISwapChain* m_dxgi_swap_chain;

	std::vector<s_sprite> m_sprites;
	s_shader m_shaders[k_shader_count];
	s_surface m_surfaces[k_surface_count];
};

inline c_rasterizer* rasterizer() {
	return &c_rasterizer::g_rasterizer;
}
