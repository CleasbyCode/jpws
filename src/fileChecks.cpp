
bool hasValidFilename(const std::string& filename) {
	return std::all_of(filename.begin(), filename.end(), 
        	[](char c) { return std::isalnum(c) || c == '.' || c == '/' || c == '\\' || c == '-' || c == '_' || c == '@' || c == '%'; });
}

bool hasValidImageExtension(const std::string& ext) {
	static const std::set<std::string> valid_extensions = {".jpg", ".jpeg", ".jfif"};
    	return valid_extensions.count(ext) > 0;
}

bool hasValidPowershellExtension(const std::string& ext) {
    	return ext == ".ps1";
}

void validateFiles(const std::string& image_file, const std::string& powershell_file) {
	
	std::filesystem::path image_path(image_file), powershell_path(powershell_file);

    	std::string 
		image_ext = image_path.extension().string(),
		powershell_ext = powershell_path.extension().string();

    	if (!hasValidImageExtension(image_ext)) {
        	throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
    	}

	if (!hasValidPowershellExtension(powershell_ext)) {
        	throw std::runtime_error("File Type Error: Invalid file extension. Only expecting \".ps1\" PowerShell extension.");
    	}

    	if (!std::filesystem::exists(image_path)) {
        	throw std::runtime_error("Image File Error: File not found.");
    	}

    	if (!std::filesystem::exists(powershell_path) || !std::filesystem::is_regular_file(powershell_path)) {
        	throw std::runtime_error("PowerShell File Error: File not found or not a regular file.");
    	}

	constexpr uintmax_t 
		MAXIMUM_IMAGE_SIZE = 4ULL * 1024 * 1024,
		MAXIMUM_SCRIPT_SIZE = 10ULL * 1024;

    	constexpr uint8_t 
		MINIMUM_IMAGE_SIZE = 134,
		MINIMUM_SCRIPT_SIZE = 10;


    	if (MINIMUM_IMAGE_SIZE > std::filesystem::file_size(image_path)) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}

	if (std::filesystem::file_size(image_path) > MAXIMUM_IMAGE_SIZE) {
   		throw std::runtime_error("Image Size Error: Size of cover image exceeds maximum size limit.");
   	}
	
	if (MINIMUM_SCRIPT_SIZE > std::filesystem::file_size(powershell_path)) {
        	throw std::runtime_error("PowerShell File Error: Invalid file size.");
    	}
	
	if (std::filesystem::file_size(powershell_path) > MAXIMUM_SCRIPT_SIZE) {
		throw std::runtime_error("PowerShell File Error: Size of script file exceeds maximum size limit.");
	}
}