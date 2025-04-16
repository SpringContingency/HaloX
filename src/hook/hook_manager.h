#pragma once

#include "./patch_manager.h"

class c_hook_manager {
public:
	c_hook_manager();
	~c_hook_manager();

    int initialize();
    int shutdown();

    void* create(void* pTarget, void* pDetour, void** ppOriginal);
	int enable(void* pTarget);
	int disable(void* pTarget);
	int remove(void* pTarget);

	static bool is_executable_address(void* address);

private:
	struct s_hook_manager_private* m_private;

};

inline c_hook_manager g_hook_manager;

inline c_hook_manager* hook_manager() {
    return &g_hook_manager;
}
