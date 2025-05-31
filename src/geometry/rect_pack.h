#pragma once

#include <vector>
#include <libmcc/libmcc.h>

enum e_rect_pack_method {
	_rect_pack_method_pixel_scan,
};

class c_rect_packer {
public:
	c_rect_packer(int width, int height, e_rect_pack_method method);
	~c_rect_packer();

	int pack(int width, int height, int* left, int* top);
private:
	void* m_data;
	int m_width, m_height;
	e_rect_pack_method m_method;
};