#include "win32.h"

#include "../common.h"

#include <Windows.h>
#include <DbgHelp.h>

class c_stack_walker : public i_stack_walker {
public:
	void free() override {
		SymCleanup(GetCurrentProcess());
		delete this;
	}

	bool next() override {
		DWORD64 displacement = 0;
		HANDLE process = GetCurrentProcess();

		name.clear();
		offset = 0;
		address = 0;
		symbol_name = nullptr;
		module_name = nullptr;

		bool result = StackWalk64(
			IMAGE_FILE_MACHINE_AMD64,
			process,
			GetCurrentThread(),
			&frame,
			&context,
			nullptr,
			SymFunctionTableAccess64,
			SymGetModuleBase64,
			nullptr);

		if (!result) {
			return false;
		}

		result = SymGetModuleInfo64(
			process, 
			frame.AddrPC.Offset, 
			&module_info);

		if (result) {
			module_name = module_info.ModuleName;
		}

		address = frame.AddrPC.Offset;

		result = SymFromAddr(
			process,
			frame.AddrPC.Offset, 
			&displacement, 
			symbol);

		if (result) {
			symbol_name = symbol->Name;
			address = symbol->Address;
			offset = symbol->Address - symbol->ModBase;
		}

		std::vector<char> buffer(MAX_SYM_NAME + MAX_PATH);

		buffer[0] = 0;

		if (module_name) {
			if (symbol_name) {
				sprintf(buffer.data(), "%s:%llX!%s", module_name, offset, symbol_name);
			} else {
				sprintf(buffer.data(), "%s:%llX", module_name, offset);
			}
		} else {
			sprintf(buffer.data(), "%llX", address);
		}

		name = buffer.data();

		return true;
	}

	const char* get_symbol_name() override {
		return symbol_name;
	}

	const char* get_module_name() override {
		return module_name;
	}

	uintptr_t get_address() override {
		return address;
	}

	uintptr_t get_offset() override {
		return offset;
	}

	const char* get_name() override {
		return name.c_str();
	}

	CONTEXT context;
	STACKFRAME64 frame;
	PSYMBOL_INFO symbol;	
	IMAGEHLP_MODULE64 module_info;
	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	std::string name;
	const char* symbol_name;
	const char* module_name;
	uintptr_t address;
	uintptr_t offset;
};

i_stack_walker* i_stack_walker::create() {
	auto walker = new c_stack_walker();

	SymInitialize(GetCurrentProcess(), nullptr, TRUE);

	RtlCaptureContext(&walker->context);

	ZeroMemory(&walker->frame, sizeof(STACKFRAME64));

	walker->frame.AddrPC.Mode = AddrModeFlat;
	walker->frame.AddrStack.Mode = AddrModeFlat;
	walker->frame.AddrFrame.Mode = AddrModeFlat;

	walker->frame.AddrPC.Offset = walker->context.Rip;
	walker->frame.AddrStack.Offset = walker->context.Rsp;
	walker->frame.AddrFrame.Offset = walker->context.Rbp;

	walker->symbol = reinterpret_cast<PSYMBOL_INFO>(walker->buffer);

	walker->symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	walker->symbol->MaxNameLen = MAX_SYM_NAME;

	ZeroMemory(&walker->module_info, sizeof(walker->module_info));
	walker->module_info.SizeOfStruct = sizeof(walker->module_info);

	if (!walker->next()) {
		walker->free();
		return nullptr;
	}

	return walker;
}
