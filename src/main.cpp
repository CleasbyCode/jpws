//	JPG-PowerShell Polyglot for X/Twitter (jpws v1.2) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024
//
//	To compile program (Linux):
// 	$ sudo apt-get install libturbojpeg-dev
// 	$ g++ main.cpp -O2 -lturbojpeg -s -o jpws
//	$ sudo cp jpws /usr/bin

// 	Run it:
// 	$ jpws

#include "jpws.h"

int main(int argc, char** argv) {
	try {
		ProgramArgs args = ProgramArgs::parse(argc, argv);
		if (!hasValidFilename(args.image_file) || !hasValidFilename(args.powershell_file)) {
            		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        	}
        	validateFiles(args.image_file, args.powershell_file);

        	jpws(args.image_file, args.powershell_file, args.lastBlockString);
    	}
	catch (const std::runtime_error& e) {
        	std::cerr << "\n" << e.what() << "\n\n";
        	return 1;
    	}
}