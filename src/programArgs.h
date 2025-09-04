#pragma once

#include <string>

enum class ArgOption {
	Default,
	Alt
};

struct ProgramArgs {
	ArgOption lastBlockString = ArgOption::Default;
    	std::string image_file;
    	std::string script_file;

    	static ProgramArgs parse(int argc, char** argv);
};
