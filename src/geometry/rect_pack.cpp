#include "rect_pack.h"

struct s_rect_packer_data {
	virtual void free() = 0;
	virtual int pack(int width, int height, int* left, int* top) = 0;
};

struct s_pixel_scan_data : s_rect_packer_data {
	struct s_surface {
		bool pixels[0];
	};

	s_pixel_scan_data(int width, int height) : width(width), height(height){
	}

	~s_pixel_scan_data() {
		for (auto surface : surfaces) {
			delete surface;
		}
	};

	void free() override {
		s_pixel_scan_data::~s_pixel_scan_data();
	}

	static bool check_rect(bool* pixels, int left, int top, int width, int height, int pitch) {
		for (int y = top; y < top + height; ++y) {
			for (int x = left; x < left + width; ++x) {
				auto offset = y * pitch + x;

				if (pixels[offset]) {
					return false;
				}
			}
		}

		return true;
	}

	static void fill_rect(bool* pixels, int left, int top, int width, int height, int pitch) {
		for (int y = top; y < top + height; ++y) {
			for (int x = left; x < left + width; ++x) {
				auto offset = y * pitch + x;

				pixels[offset] = true;
			}
		}
	}

	int pack(int width, int height, int* left, int* top) override {
		if (this->width < width || this->height < height) {
			return -1;
		}

		auto size = surfaces.size();

		for (int i = 0; i <= size; ++i) {
			if (i == size) {
				if (!add_surface()) {
					return -1;
				}
			}

			auto surface = surfaces[i];

			for (int y = 0; y < this->height; ++y) {
				if (y + height > this->height) {
					break;
				}

				for (int x = 0; x < this->width; ++x) {
					if (x + width > this->width) {
						break;
					}

					auto offset = y * this->width + x;

					if (!check_rect(surface->pixels, x, y, width, height, this->width)) {
						continue;
					}

					fill_rect(surface->pixels, x, y, width, height, this->width);

					*left = x;
					*top = y;

					return i;
				}
			}
		}

		return -1;
	}

	s_surface* add_surface() {
		auto size = sizeof(s_surface) + 1ull * width * height;
		auto surface = reinterpret_cast<s_surface*>(new char[size]);

		memset(surface, 0, size);
		surfaces.push_back(surface);

		return surface;
	}

	int width, height;
	std::vector<s_surface*> surfaces;
};

c_rect_packer::c_rect_packer(int width, int height, e_rect_pack_method method) 
	: m_width(width), m_height(height), m_method(method), m_data(nullptr) {
	switch (method) {
	case _rect_pack_method_pixel_scan:
		m_data = new s_pixel_scan_data(width, height);
		break;
	default:
		break;
	}
}

c_rect_packer::~c_rect_packer() {
	if (!m_data) {
		return;
	}

	reinterpret_cast<s_rect_packer_data*>(m_data)->free();
	m_data = nullptr;
}

int c_rect_packer::pack(int width, int height, int* left, int* top) {
	if (m_data == nullptr) {
		return -1; // not initialized
	}

	return reinterpret_cast<s_rect_packer_data*>(m_data)->pack(width, height, left, top);
}
