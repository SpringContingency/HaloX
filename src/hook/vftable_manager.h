#pragma once

#include "../common.h"

class c_vftable_manager {
public:
	c_vftable_manager();
	~c_vftable_manager();

	int initialize();
	int shutdown();

	void* create(void* pTarget, const void** pPatch, size_t size, void** ppOriginal);
	int enable(void* pTarget);
	int disable(void* pTarget);
	int remove(void* pTarget);

private:
	struct s_vftable_manager_private* m_private;

};

inline c_vftable_manager g_vftable_manager;

inline c_vftable_manager* vftable_manager() {
	return &g_vftable_manager;
}
