#include "tool.h"

#include "import_unreal_font.h"
#include "import_windows_font.h"

#include <iostream>

static s_command* commands[] {
	&g_command_import_unreal_font,
	&g_command_import_windows_font,
};

static constexpr int command_count = sizeof(commands) / sizeof(s_command*);

int main(int argc, const char** argv) {
	if (argc < 2) {
		printf("Usage: tool <command> [args]\n");
		
		for (int i = 0; i < command_count; ++i) {
			auto command = commands[i];
			printf("\t%s: %s\n", command->name, command->description);
		}

		return 0;
	}

	auto command_name = argv[1];
	auto command_argc = argc - 2;
	auto command_argv = argv + 2;

	for (int i = 0; i < command_count; ++i) {
		auto command = commands[i];
		if (strcmp(command->name, command_name) == 0) {
			return command->proc(command_argc, command_argv);
		}
	}

	return 0;
}
