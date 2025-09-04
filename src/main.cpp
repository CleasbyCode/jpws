// JPG-PowerShell Polyglot for X-Twitter (jpws v1.3) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024.

// CLI source code (Linux / Windows).

// Compile program (Linux):

// $ sudo apt-get install libturbojpeg0-dev

// $ chmod +x compile_jpws.sh
// $ ./compile_jpws.sh
	
// $ Compilation successful. Executable 'jpws' created.
// $ sudo cp jpws /usr/bin
// $ jpws

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image/stb_image_resize2.h"

// stb_image by Sean Barrett (“nothings”).
// https://github.com/nothings/stb

#include <turbojpeg.h>

// This software is based in part on the work of the Independent JPEG Group.
// Copyright (C) 2009-2024 D. R. Commander. All Rights Reserved.
// Copyright (C) 2015 Viktor Szathmáry. All Rights Reserved.
// https://github.com/libjpeg-turbo/libjpeg-turbo

#include "fileChecks.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <random>
#include <cstdint>
#include <fstream>
#include <iostream>

// Return vector index location for relevant signature search.
template <typename T, size_t N>
static inline uint32_t searchSig(std::vector<uint8_t>& vec, const std::array<T, N>& SIG) {
	return static_cast<uint32_t>(std::search(vec.begin(), vec.end(), SIG.begin(), SIG.end()) - vec.begin());
}

static inline void resizeImage(std::vector<uint8_t>& image_vec, uint8_t quality_val, uint16_t decrease_dims_val, bool shouldDecreaseVals) {
	tjhandle decompressor = tjInitDecompress();
    	if (!decompressor) {
        	throw std::runtime_error("tjInitDecompress() failed.");
    	}	

    	int width = 0, height = 0;
    	int jpegSubsamp = 0, jpegColorspace = 0;

    	if (tjDecompressHeader3(decompressor, image_vec.data(), static_cast<unsigned long>(image_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
        	tjDestroy(decompressor);
        	throw std::runtime_error(std::string("tjDecompressHeader3: ") + tjGetErrorStr());
    	}

    	if (width < decrease_dims_val || height < decrease_dims_val) {
        	tjDestroy(decompressor);
        	throw std::runtime_error("Image is too small to decrease by 1 pixel.");
    	}

    	const int channels = 3;
    	std::vector<uint8_t> decoded_image_vec(width * height * channels);

    	if (tjDecompress2(decompressor, image_vec.data(), static_cast<unsigned long>(image_vec.size()), decoded_image_vec.data(), width, 0, height, TJPF_RGB, 0) != 0) {
        	tjDestroy(decompressor);
        	throw std::runtime_error(std::string("tjDecompress2: ") + tjGetErrorStr());
    	}

    	tjDestroy(decompressor);

    	int newWidth = 0;
    	int newHeight = 0;

    	if (shouldDecreaseVals) {
        	newWidth  = width  - decrease_dims_val;
        	newHeight = height - decrease_dims_val;
    	} else {
        	newWidth  = width;
        	newHeight = height;
    	}

    	std::cout << "\r" << std::string(44, ' ') << "\r"; 
    	std::cout << "Quality: " << (int)quality_val << "% | Width: " << newWidth << " | Height: " << newHeight << std::flush; 

    	std::vector<uint8_t> resized_image_vec(newWidth * newHeight * channels);

    	if (!stbir_resize_uint8_srgb(decoded_image_vec.data(), width, height, 0, resized_image_vec.data(), newWidth, newHeight, 0, static_cast<stbir_pixel_layout>(channels))) {
        	throw std::runtime_error("stbir_resize_uint8_srgb failed.");
    	}

    	tjhandle compressor = tjInitCompress();
    	if (!compressor) {
        	throw std::runtime_error("tjInitCompress() failed.");
    	}

    	unsigned char* jpegBuf  = nullptr;
    	unsigned long  jpegSize = 0;

    	int flags = TJFLAG_PROGRESSIVE;  

    	if (tjCompress2(compressor, resized_image_vec.data(), newWidth, 0, newHeight, TJPF_RGB, &jpegBuf, &jpegSize, jpegSubsamp, quality_val, flags) != 0) {
        	tjDestroy(compressor);
        	throw std::runtime_error(std::string("tjCompress2: ") + tjGetErrorStr());
    	}

    	tjDestroy(compressor);

    	std::vector<uint8_t> output_image_vec(jpegBuf, jpegBuf + jpegSize);
    	tjFree(jpegBuf);

    	image_vec.swap(output_image_vec);
}

int main(int argc, char** argv) {
	try {
		ProgramArgs args = ProgramArgs::parse(argc, argv);
		
		std::vector<uint8_t> 
        		image_vec,
        		script_vec;
		
		uintmax_t image_file_size = 0;
		
		validateImageFile(args.image_file, image_file_size, image_vec);
		
		constexpr std::array<uint8_t, 2>
			COMMENT_BLOCK_SIG 	{ 0x23, 0x3E },
			APP1_SIG 		{ 0xFF, 0xE1 }, // EXIF SEGMENT MARKER.
			APP2_SIG 		{ 0xFF, 0xE2 }; // ICC COLOR PROFILE SEGMENT MARKER.

		constexpr std::array<uint8_t, 4>
			DQT1_SIG { 0xFF, 0xDB, 0x00, 0x43 },
			DQT2_SIG { 0xFF, 0xDB, 0x00, 0x84 };
		
		const uint32_t APP1_POS = searchSig(image_vec, APP1_SIG);

		// Remove superfluous segments from cover image. (EXIF, ICC color profile, etc).

		if (image_vec.size() > APP1_POS) {
			const uint16_t APP1_BLOCK_SIZE = (static_cast<uint16_t>(image_vec[APP1_POS + 2]) << 8) | static_cast<uint16_t>(image_vec[APP1_POS + 3]);
			image_vec.erase(image_vec.begin() + APP1_POS, image_vec.begin() + APP1_POS + APP1_BLOCK_SIZE + 2);
		}

		const uint32_t APP2_POS = searchSig(image_vec, APP2_SIG);
		if (image_vec.size() > APP2_POS) {
			const uint16_t APP2_BLOCK_SIZE = (static_cast<uint16_t>(image_vec[APP2_POS + 2]) << 8) | static_cast<uint16_t>(image_vec[APP2_POS + 3]);
			image_vec.erase(image_vec.begin() + APP2_POS, image_vec.begin() + APP2_POS + APP2_BLOCK_SIZE + 2);
		}

		const uint32_t
			DQT1_POS = searchSig(image_vec, DQT1_SIG),
			DQT2_POS = searchSig(image_vec, DQT2_SIG),
			DQT_POS  = std::min(DQT1_POS, DQT2_POS);

		image_vec.erase(image_vec.begin(), image_vec.begin() + DQT_POS);
		// ------------
	
		constexpr uint8_t 
			COMPATIBLE_IMAGE_VAL = 0x19,
			JIFF_SIG_LENGTH = 20;
		
		constexpr std::array<uint8_t, JIFF_SIG_LENGTH> JFIF_SIG	{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
	
		image_vec.insert(image_vec.begin(), JFIF_SIG.begin(), JFIF_SIG.end());

		std::vector<uint8_t>image_vec_copy;
		image_vec_copy = image_vec;
	
		bool 
			shouldDecreaseVals = false,
			isImageModified = false;

		std::cout << '\n';

		if  (image_vec[0x0D] != COMPATIBLE_IMAGE_VAL) {	

			uint8_t quality_val = 97;	
			uint16_t 
				decrease_attempts = 300,
				decrease_dims_val = 0;

			std::cout << "Checking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n\n"
			  		<< "Image quality & dimensions will be reduced in an attempt to remove these sequences.\n\n";

			resizeImage(image_vec, quality_val, decrease_dims_val, shouldDecreaseVals);
		
			isImageModified = true;

			uint32_t comment_block_pos = searchSig(image_vec, COMMENT_BLOCK_SIG);

			shouldDecreaseVals = true;

			while(comment_block_pos != image_vec.size()) {
				// image_vec.clear();
				std::vector<uint8_t>().swap(image_vec);
				image_vec = image_vec_copy; 

				--decrease_attempts;
				++decrease_dims_val;
				quality_val -= (decrease_attempts % 15 == 0) ? 2 : 0;
				
				resizeImage(image_vec, quality_val, decrease_dims_val, shouldDecreaseVals);

				comment_block_pos = searchSig(image_vec, COMMENT_BLOCK_SIG);

				if (!decrease_attempts) {
		  			std::cerr << "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.\n";
			  		throw std::runtime_error("Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.");
				}
			}
		}

		std::vector<uint8_t>().swap(image_vec_copy);

		std::cout << '\n';

		constexpr std::array<uint8_t, 11>
			DEFAULT_BYTES 	{ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e },
			ALT_BYTES	{ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 }; 

		if (args.lastBlockString == ArgOption::Alt) {
			std::copy(ALT_BYTES.rbegin(), ALT_BYTES.rend(), image_vec.rbegin() + 2);
		} else {
			std::copy(DEFAULT_BYTES.rbegin(), DEFAULT_BYTES.rend(), image_vec.rbegin() + 2);
		}
	
		constexpr uint8_t POWERSHELL_INSERT_INDEX = 6;
			
		validateScriptFile(args.script_file, script_vec);
	
		std::vector<uint8_t>profile_vec = { 
			0xFF, 0xE2, 0x00, 0x00, 0x49, 0x43, 0x43, 0x5F, 0x50, 0x52, 0x4F, 0x46, 0x49, 0x4C, 0x45, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x6A, 0x70, 0x77, 
			0x73, 0x5F, 0x00, 0x00, 0x6D, 0x6E, 0x74, 0x72, 0x52, 0x47, 0x42, 0x20, 0x58, 0x59, 0x5A, 0x20, 0x07, 0xE2, 0x00, 0x03, 0x00, 0x14, 0x00, 0x09, 0x00, 0x0E, 
			0x00, 0x1D, 0x61, 0x63, 0x73, 0x70, 0x4D, 0x53, 0x46, 0x54, 0x00, 0x00, 0x00, 0x00, 0x73, 0x61, 0x77, 0x73, 0x63, 0x74, 0x72, 0x6C, 0x00, 0x00, 0x00, 0x00, 
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF6, 0xD6, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x2D, 0x68, 0x61, 0x6E, 0x64, 0xEB, 0x77, 
			0x1F, 0x3C, 0xAA, 0x53, 0x51, 0x02, 0xE9, 0x3E, 0x28, 0x6C, 0x91, 0x46, 0xAE, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x64, 0x65, 0x73, 0x63, 0x00, 0x00,
			0x00, 0xF0, 0x00, 0x00, 0x00, 0x1C, 0x77, 0x74, 0x70, 0x74, 0x00, 0x00, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x72, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x20,
			0x00, 0x00, 0x00, 0x14, 0x67, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x34, 0x00, 0x00, 0x00, 0x14, 0x62, 0x58, 0x59, 0x5A, 0x00, 0x00, 0x01, 0x48, 0x00, 0x00,
			0x00, 0x14, 0x72, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x67, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34,
			0x62, 0x54, 0x52, 0x43, 0x00, 0x00, 0x01, 0x5C, 0x00, 0x00, 0x00, 0x34, 0x63, 0x70, 0x72, 0x74, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x01, 0x64, 0x65,
			0x73, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x6E, 0x52, 0x47, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
			0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x54, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16, 0xC9, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x6F, 0xA0, 0x00, 0x00, 0x38, 0xF2, 0x00, 0x00, 0x03, 0x8F, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x62, 0x96,
			0x00, 0x00, 0xB7, 0x89, 0x00, 0x00, 0x18, 0xDA, 0x58, 0x59, 0x5A, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xA0, 0x00, 0x00, 0x0F, 0x85, 0x00, 0x00,
			0xB6, 0xC4, 0x63, 0x75, 0x72, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x01, 0x07, 0x02, 0xB5, 0x05, 0x6B, 0x09, 0x36, 0x0E, 0x50,
			0x14, 0xB1, 0x1C, 0x80, 0x25, 0xC8, 0x30, 0xA1, 0x3D, 0x19, 0x4B, 0x40, 0x5B, 0x27, 0x6C, 0xDB, 0x80, 0x6B, 0x95, 0xE3, 0xAD, 0x50, 0xC6, 0xC2, 0xE2, 0x31,
			0xFF, 0xFF, 0x23, 0x3E, 0x63, 0x6C, 0x73, 0x3B, 0x0D, 0x0A, 0x3C, 0x23, 0x0D, 0x0A 
		};
	
		profile_vec.insert(profile_vec.end() - POWERSHELL_INSERT_INDEX, script_vec.begin(), script_vec.end());

		std::vector<uint8_t>().swap(script_vec);

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
			throw std::runtime_error("Segment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.");
		}

		image_vec.insert(image_vec.begin() + JIFF_SIG_LENGTH , profile_vec.begin(), profile_vec.end());
		
		std::vector<uint8_t>().swap(profile_vec);

		while (bits) {
			image_vec[segment_size_field_index++] = (SEGMENT_SIZE >> (bits -= 8)) & 0xFF;
		}

		bits = 32;
	
		while (bits) {
			image_vec[profile_size_field_index++] = (PROFILE_SIZE >> (bits -= 8)) & 0xFF;
		}
	
		constexpr std::array<uint8_t, 6> JFIF_COMMENT_BLOCK {0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23};

		std::copy(JFIF_COMMENT_BLOCK.begin(), JFIF_COMMENT_BLOCK.end(), image_vec.begin() + jfif_comment_block_index);

		std::random_device rd;
    		std::mt19937 gen(rd());
    		std::uniform_int_distribution<> dist(10000, 99999);  // Five-digit random number

		const std::string OUTPUT_FILENAME = "jpws_" + std::to_string(dist(gen)) + ".jpg";

		std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

		if (!file_ofs) {
			throw std::runtime_error("Write Error: Unable to write to file.");
		}
	
		const uint32_t IMAGE_SIZE = static_cast<uint32_t>(image_vec.size());

		file_ofs.write(reinterpret_cast<const char*>(image_vec.data()), IMAGE_SIZE);
		file_ofs.close();
	
		std::vector<uint8_t>().swap(image_vec);
	
		std::cout << "\nSaved JPG-PowerShell polyglot image: " << OUTPUT_FILENAME << " (" << IMAGE_SIZE << " bytes).\n";

		if (isImageModified) {
			std::cout << "\nComment-block close sequences succesfully removed from image.\n"
					  << "\nPlease check to make sure size & quality of cover image is acceptable.\n";
		}
		std::cout << "\nComplete!\n\n";
		return 0;
	}
	catch (const std::runtime_error& e) {
        	std::cerr << "\n" << e.what() << "\n\n";
        	return 1;
    	}
}
