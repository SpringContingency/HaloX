#pragma once

#include "../common.h"

class c_patch_manager {
public:
	c_patch_manager();
	~c_patch_manager();

	int initialize();
	int shutdown();

	void* create(void* pTarget, const char* pPatch);
	void* create(void* pTarget, const void* pPatch, size_t size);
	int enable(void* pTarget);
	int disable(void* pTarget);
	int remove(void* pTarget);

	static bool write_memory(void* dst, void* srx, size_t size);

private:
	struct s_patch_manager_private* m_private;

};

inline c_patch_manager g_patch_manager;

inline c_patch_manager* patch_manager() {
	return &g_patch_manager;
}
