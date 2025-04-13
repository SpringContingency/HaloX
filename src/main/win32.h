#pragma once

#include <Windows.h>
#include <winnt.h>
#include <winternl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

__forceinline PEB* NtCurrentPeb() {return reinterpret_cast<PEB*>(__readgsqword(0x60));}

std::wstring win32_get_document_folder_path();

class i_stack_walker {
public:
	static i_stack_walker* create();

	virtual void free() = 0;
	virtual bool next() = 0;
	virtual const char* get_name() = 0;
	virtual const char* get_symbol_name() = 0;
	virtual const char* get_module_name() = 0;
	virtual uintptr_t get_address() = 0;
	virtual uintptr_t get_offset() = 0;
};

