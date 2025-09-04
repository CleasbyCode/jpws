#include "fileChecks.h"

#include <stdexcept>
#include <cctype>
#include <algorithm>
#include <set>
#include <filesystem>
#include <fstream> 
#include <array>
#include <iostream>

bool hasValidFilename(const std::string& filename) {
	return std::all_of(filename.begin(), filename.end(), [](char c) { return std::isalnum(c) || c == '.' || c == '/' || c == '\\' || c == '-' || c == '_' || c == '@' || c == '%'; });
}

bool hasValidImageExtension(const std::string& ext) {
	static const std::set<std::string> valid_extensions = {".jpg", ".jpeg", ".jfif"};
	return valid_extensions.count(ext) > 0;
}

bool hasValidScriptExtension(const std::string& ext) {
    	return ext == ".ps1";
}

void validateImageFile(std::string& image_file, uintmax_t& image_file_size, std::vector<uint8_t>& image_vec) {
	std::filesystem::path image_path(image_file);

    	std::string image_ext = image_path.extension().string();

	if (!std::filesystem::exists(image_path)) {
        	throw std::runtime_error("Image File Error: File not found.");
    	}

	if (!hasValidFilename(image_path.string())) {
    		throw std::runtime_error("Invalid Input Error: Unsupported characters in image filename.");
    	}

    	if (!hasValidImageExtension(image_ext)) {
        	throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
    	}

	std::ifstream image_file_ifs(image_path, std::ios::binary);
        	
    	if (!image_file_ifs) {
    		throw std::runtime_error("Read File Error: Unable to read image file. Check the filename and try again.");
	}

    	image_file_size = std::filesystem::file_size(image_path);

    	constexpr uint8_t MIN_IMAGE_SIZE = 134;

    	if (MIN_IMAGE_SIZE > image_file_size) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}

    	constexpr uintmax_t MAX_IMAGE_SIZE = 4ULL * 1024 * 1024;
    
    	if (image_file_size > MAX_IMAGE_SIZE) {
   		throw std::runtime_error("Image Size Error: Size of cover image exceeds maximum size limit.");
   	}
    
	std::vector<uint8_t> tmp_vec(image_file_size);
	
	image_file_ifs.read(reinterpret_cast<char*>(tmp_vec.data()), image_file_size);
	image_file_ifs.close();
	
	image_vec.swap(tmp_vec);
	std::vector<uint8_t>().swap(tmp_vec);
	
	constexpr std::array<uint8_t, 2>
		IMAGE_START_SIG	{ 0xFF, 0xD8 },
		IMAGE_END_SIG   { 0xFF, 0xD9 };

	if (!std::equal(IMAGE_START_SIG.begin(), IMAGE_START_SIG.end(), image_vec.begin()) || !std::equal(IMAGE_END_SIG.begin(), IMAGE_END_SIG.end(), image_vec.end() - 2)) {
    		throw std::runtime_error("Image File Error: This is not a valid JPG image.");
	}
}

void validateScriptFile(std::string& script_file, std::vector<uint8_t>& script_vec) {
	std::filesystem::path script_path(script_file);

    	std::string script_ext = script_path.extension().string();

	if (!hasValidScriptExtension(script_ext)) {
        	throw std::runtime_error("File Type Error: Invalid file extension. Only expecting \".ps1\" PowerShell extension.");
    	}

	if (!hasValidFilename(script_path.string())) {
    		throw std::runtime_error("Invalid Input Error: Unsupported characters in script filename.");
    	}

    	if (!std::filesystem::exists(script_path) || !std::filesystem::is_regular_file(script_path)) {
        	throw std::runtime_error("PowerShell File Error: File not found or not a regular file.");
    	}

	std::ifstream script_file_ifs(script_path, std::ios::binary);
        	
   	if (!script_file_ifs) {
    		throw std::runtime_error("Read File Error: Unable to read script file. Check the filename and try again.");
	}

	uintmax_t script_file_size = std::filesystem::file_size(script_path);

	constexpr uint16_t MAX_SCRIPT_SIZE = 10ULL * 1024;

    	constexpr uint8_t MIN_SCRIPT_SIZE = 10;
	
	if (MIN_SCRIPT_SIZE > script_file_size) {
        	throw std::runtime_error("PowerShell File Error: Invalid file size.");
    	}
	
	if (script_file_size > MAX_SCRIPT_SIZE) {
		throw std::runtime_error("PowerShell File Error: Size of script file exceeds maximum size limit.");
	}
	
	std::vector<uint8_t> tmp_vec(script_file_size);
	
	script_file_ifs.read(reinterpret_cast<char*>(tmp_vec.data()), script_file_size);
	script_file_ifs.close();
	
	script_vec.swap(tmp_vec);
	std::vector<uint8_t>().swap(tmp_vec);
	
	constexpr std::array<uint8_t, 3> BOM_SIG { 0xEF, 0xBB, 0xBF };
	
	if (std::equal(BOM_SIG.begin(), BOM_SIG.end(), script_vec.begin())) {
        	script_vec.erase(script_vec.begin(), script_vec.begin() + 3);
        }		
}
