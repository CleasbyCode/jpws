#include "programArgs.h"
#include "information.h"       
#include <stdexcept>       
#include <cstdlib> 

ProgramArgs ProgramArgs::parse(int argc, char** argv) {
	ProgramArgs args;
	if (argc == 2 && std::string(argv[1]) == "--info") {
		displayInfo();
        std::exit(0);
	}

	if (argc < 3 || argc > 4) {
    	throw std::runtime_error("Usage: jpws [-alt] <cover_image> <powershell_script>\n\t\bjpws --info");
    }

    uint8_t arg_index = 1;

    if (argc == 4) {
		if (std::string(argv[arg_index]) != "-alt") {
        	throw std::runtime_error("Input Error: Invalid arguments. Expecting \"-alt\" as the only optional argument.");
        }
        args.lastBlockString = ArgOption::Alt;
       	arg_index = 2;
    }

    args.image_file = argv[arg_index];
    args.script_file = argv[++arg_index];
    return args;
}
