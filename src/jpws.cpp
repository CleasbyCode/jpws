int jpws(const std::string& IMAGE_FILENAME, const std::string& POWERSHELL_FILENAME, ArgOption lastBlockString) {
	bool shouldEncodeImage = false;

	std::ifstream
		image_file_ifs(IMAGE_FILENAME, std::ios::binary),
		powershell_file_ifs(POWERSHELL_FILENAME, std::ios::binary);

	if (!image_file_ifs || !powershell_file_ifs) {
		std::cerr << "\nRead File Error: Unable to read " << (!image_file_ifs 
			? "image file" 
			: "PowerShell file") << ".\n\n";
		return 1;
	}

	const uintmax_t
		IMAGE_FILE_SIZE 	= std::filesystem::file_size(IMAGE_FILENAME),
		POWERSHELL_FILE_SIZE 	= std::filesystem::file_size(POWERSHELL_FILENAME);

	std::vector<uint8_t> image_vec;
	image_vec.resize(IMAGE_FILE_SIZE); 
	
	image_file_ifs.read(reinterpret_cast<char*>(image_vec.data()), IMAGE_FILE_SIZE);
	image_file_ifs.close();

	std::vector<uint8_t> powershell_vec;
	powershell_vec.resize(POWERSHELL_FILE_SIZE); 
	
	powershell_file_ifs.read(reinterpret_cast<char*>(powershell_vec.data()), POWERSHELL_FILE_SIZE);
	powershell_file_ifs.close();

	constexpr std::array<uint8_t, 20> JFIF_SIG 	{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
	constexpr std::array<uint8_t, 3> BOM_SIG	{ 0xEF, 0xBB, 0xBF };

	constexpr std::array<uint8_t, 2> 
		SOI_SIG			{ 0xFF, 0xD8 }, 
		EOI_SIG			{ 0xFF, 0xD9 }, 
		COMMENT_BLOCK_SIG	{ 0x23, 0x3E };
 
	constexpr uint8_t JIFF_SIG_LENGTH = 20;

	if (!std::equal(SOI_SIG.begin(), SOI_SIG.end(), image_vec.begin()) || !std::equal(EOI_SIG.begin(), EOI_SIG.end(), image_vec.end() - 2)) {
        	std::cerr << "\nImage File Error: This is not a valid JPG image.\n\n";
		return 1;
	}

	constexpr uint8_t COMPATIBLE_IMAGE_VAL = 0x19;

	if (image_vec[0x0D] != COMPATIBLE_IMAGE_VAL) {
		shouldEncodeImage = true;
	}
	
	eraseSegments(image_vec);
	
	image_vec.insert(image_vec.begin(), JFIF_SIG.begin(), JFIF_SIG.end());

	std::vector<uint8_t>image_vec_copy;
	image_vec_copy = image_vec;

	if (std::equal(BOM_SIG.begin(), BOM_SIG.end(), powershell_vec.begin())) {
        	powershell_vec.erase(powershell_vec.begin(), powershell_vec.begin() + 3);
        }

	bool 
		shouldDecreaseVals = false,
		isImageModified = false;

	std::cout << '\n';

	if  (shouldEncodeImage) {	

		uint8_t quality_val = 97;
			
		uint16_t 
			decrease_attempts = 300,
			decrease_dims_val = 0;

		std::cout << "Checking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n\n"
			  << "Image quality & dimensions will be reduced in an attempt to remove these sequences.\n\n";

		resizeImage(image_vec, quality_val, decrease_dims_val, shouldDecreaseVals);
		
		isImageModified = true;

		uint32_t comment_block_pos = searchFunc(image_vec, 0, 0, COMMENT_BLOCK_SIG);

		shouldDecreaseVals = true;

		while(comment_block_pos != image_vec.size()) {
			image_vec.clear();
			image_vec = image_vec_copy; 

			--decrease_attempts;
			++decrease_dims_val;
			quality_val -= (decrease_attempts % 15 == 0) ? 2 : 0;
			resizeImage(image_vec, quality_val, decrease_dims_val, shouldDecreaseVals);

			comment_block_pos = searchFunc(image_vec, 0, 0, COMMENT_BLOCK_SIG);

			if (!decrease_attempts) {
		  		std::cerr << "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.\n"
			   		 << "Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.\n\n";
	          		return 1;
			}
		}
	}

	std::vector<uint8_t>().swap(image_vec_copy);

	std::cout << '\n';

	std::vector<uint8_t> 
		comment_block_string_vec,
		default_vec 	{ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e },
		alt_vec		{ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 }; 

	if (lastBlockString == ArgOption::Alt) {
		comment_block_string_vec = alt_vec;
	} else {
		comment_block_string_vec = default_vec;
	}
	
	std::copy(comment_block_string_vec.rbegin(), comment_block_string_vec.rend(), image_vec.rbegin() + 2);

	constexpr uint8_t POWERSHELL_INSERT_INDEX = 6;

	profile_vec.insert(profile_vec.end() - POWERSHELL_INSERT_INDEX, powershell_vec.begin(), powershell_vec.end());

	std::vector<uint8_t>().swap(powershell_vec);

	uint8_t
		bits = 16,	
		jfif_comment_block_index = 0x0C,					
		segment_size_field_index = 0x16,
		profile_size_field_index = 0x26;		
		
	const size_t 
		SEGMENT_SIZE = (profile_vec.size() + JIFF_SIG_LENGTH) - segment_size_field_index,
		PROFILE_SIZE = SEGMENT_SIZE - bits;
	
	constexpr uint16_t MAX_POWERSHELL_FILE_SIZE = 10 * 1024; 

	if (SEGMENT_SIZE > MAX_POWERSHELL_FILE_SIZE) {
		std::cerr <<"\nSegment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.\n\n";
		return 1;
	}

	image_vec.insert(image_vec.begin() + JIFF_SIG_LENGTH , profile_vec.begin(), profile_vec.end());

	while (bits) {
		image_vec[segment_size_field_index++] = (SEGMENT_SIZE >> (bits -= 8)) & 0xFF;
	}

	bits = 32;
	
	while (bits) {
		image_vec[profile_size_field_index++] = (PROFILE_SIZE >> (bits -= 8)) & 0xFF;
	}
	
	constexpr std::array<uint8_t, 6> JFIF_COMMENT_BLOCK {0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23};

	std::copy(JFIF_COMMENT_BLOCK.begin(), JFIF_COMMENT_BLOCK.end(), image_vec.begin() + jfif_comment_block_index);

	if (!writeFile(image_vec)) {
		return 1;
	}

	if (isImageModified) {
		std::cout << "\nComment-block close sequences succesfully removed from image.\n"
			  << "\nPlease check to make sure size & quality of cover image is acceptable.\n";
	}
	std::cout << "\nComplete!\n\n";
	return 0;
}
