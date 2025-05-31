#pragma once

typedef int (*t_command_proc)(int argc, const char** argv);

struct s_command {
	const char* name;
	const char* description;
	t_command_proc proc;
};
