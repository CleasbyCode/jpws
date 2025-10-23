// JPG-PowerShell Polyglot for X-Twitter (jpws v1.5) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024.

// CLI source code (Linux / Windows).

// Compile program (Linux):

// $ sudo apt-get install libturbojpeg0-dev

// $ chmod +x compile_jpws.sh
// $ ./compile_jpws.sh
	
// $ Compilation successful. Executable 'jpws' created.

// $ sudo cp jpws /usr/bin
// $ jpws

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image/include/stb_image_resize2.h"
// stb_image by Sean Barrett (“nothings”).
// https://github.com/nothings/stb

#ifdef _WIN32
	#include "windows/libjpeg-turbo/include/turbojpeg.h"
#else
	#include <turbojpeg.h>
#endif
// This software is based in part on the work of the Independent JPEG Group.
// Copyright (C) 2009-2024 D. R. Commander. All Rights Reserved.
// Copyright (C) 2015 Viktor Szathmáry. All Rights Reserved.
// https://github.com/libjpeg-turbo/libjpeg-turbo

#include <algorithm>
#include <string>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <cstdlib>
#include <limits>
#include <span>
#include <string_view>
#include <vector>
#include <array>
#include <filesystem>
#include <random>
#include <cstdint>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void displayInfo() {
	std::cout << R"(

JPG-PowerShell Polyglot for X-Twitter (jpws v1.5)
Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024 

CLI tool for embedding a PowerShell script within a JPG image, 
creating a tweetable JPG-PowerShell polyglot file.

──────────────────────────
Compile & run (Linux)
──────────────────────────
		
$ sudo apt-get install libturbojpeg0-dev

$ chmod +x compile_jpws.sh
$ ./compile_jpws.sh
	
Compilation successful. Executable 'jpws' created.

$ sudo cp jpws /usr/bin
$ jpws
		
Usage: jpws [-alt] <cover_image> <powershell_script>
       jpws --info
		
Share your "PowerShell-embedded" JPG image on X-Twitter.

Max script size is ~10KB. 
Max image size is 4MB.

https://github.com/CleasbyCode/jpws

)";
}

enum class Option : unsigned char { None, Alt };

struct ProgramArgs {
	Option option{Option::None};

	fs::path image_file_path;
	fs::path pwsh_file_path;
    
	static std::optional<ProgramArgs> parse(int argc, char** argv) {
		using std::string_view;

        auto arg = [&](int i) -> string_view {
			return (i >= 0 && i < argc) ? string_view(argv[i]) : string_view{};
        };

        const std::string prog = fs::path(argv[0]).filename().string();
        const std::string USAGE =
        	"Usage: " + prog + " [-alt] <cover_image> <powershell_script>\n\t\b"
            	+ prog + " --info";
            
        auto die = [&]() -> void {
        	throw std::runtime_error(USAGE);
        };

        if (argc < 2) die();

        if (argc == 2 && arg(1) == "--info") {
        	displayInfo();
        	return std::nullopt;
        }

		ProgramArgs out{};

        const string_view opt = arg(1);

        if (opt == "-alt") {
        	int i = 2;
        	
            if (argc != 4) die();

            out.image_file_path = fs::path(arg(i));
            out.pwsh_file_path  = fs::path(arg(i + 1));
            out.option = Option::Alt;
            return out;
        } else {
        	if (argc != 3) die();
        	out.image_file_path = fs::path(arg(1));
            out.pwsh_file_path  = fs::path(arg(2));
            return out;
        }
        die();
        return out; // Keeps compiler happy.
    }
};

static bool hasValidFilename(const fs::path& p) {
	if (p.empty()) {
   		return false;
   	}
    
    std::string filename = p.filename().string();
    if (filename.empty()) {
    	return false;
    }

    auto validChar = [](unsigned char c) {
    	return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
 	};

    return std::all_of(filename.begin(), filename.end(), validChar);
}

static bool hasFileExtension(const fs::path& p, std::initializer_list<const char*> exts) {
	auto e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    for (const char* cand : exts) {
    	std::string c = cand;
        std::transform(c.begin(), c.end(), c.begin(), [](unsigned char x){ return static_cast<char>(std::tolower(x)); });
        if (e == c) return true;
    }
    return false;
}

// searchSig function searches a byte vector (uint8_t) for a fixed byte pattern and returns the offset of the first match, or std::nullopt if there’s no match.
// It uses std::search on v.begin().. v.end() with the pattern given by sig.begin().. sig.end(). 
	
// The std::span<const uint8_t> parameter lets you pass anything contiguous - std::array, C-array, another std::vector, or a subrange, without copying. 
// If std::search returns v.end(), the function maps that to std::nullopt; otherwise it converts the iterator difference to a size_t index.
// I often then convert the size_t index result to uint32_t for compatibiblty reasons for other parts of the program.
static std::optional<size_t> searchSig(const std::vector<uint8_t>& v, std::span<const uint8_t> sig) {
	auto it = std::search(v.begin(), v.end(), sig.begin(), sig.end());
	if (it == v.end()) return std::nullopt;
	return static_cast<size_t>(it - v.begin());
}

// First search for an EXIF segment, if found search for an Orientation tag.
// Returns 1..8 if found and passed to normalize_orientation, or std::nullopt if no EXIF/Orientation.
static std::optional<uint16_t> exif_orientation(const std::vector<uint8_t>& jpg) {
	const uint8_t APP1[] = {0xFF, 0xE1};
    auto app1 = searchSig(jpg, std::span<const uint8_t>(APP1, 2));
    if (!app1) return std::nullopt;

    size_t p = *app1;
    if (p + 4 > jpg.size()) return std::nullopt;

    uint16_t len = (static_cast<uint16_t>(jpg[p+2]) << 8) | jpg[p+3];
    size_t exif_end = p + 2 + len;            
    if (exif_end > jpg.size()) return std::nullopt;

    size_t exif_start = p + 4;
    if (exif_start + 6 > exif_end) return std::nullopt;
    if (std::memcmp(&jpg[exif_start], "Exif\0\0", 6) != 0) return std::nullopt;

    size_t tiff = exif_start + 6;
    if (tiff + 8 > exif_end) return std::nullopt;

    bool le = false;
    if (jpg[tiff] == 'I' && jpg[tiff+1] == 'I') le = true;
    else if (jpg[tiff] == 'M' && jpg[tiff+1] == 'M') le = false;
    else return std::nullopt;

    auto rd16 = [&](size_t off) -> uint16_t {
    	if (off + 1 >= exif_end) return 0;
    	return le ? (uint16_t)(jpg[off] | (jpg[off+1] << 8)) : (uint16_t)((jpg[off] << 8) | jpg[off+1]);
    };
	
    auto rd32 = [&](size_t off) -> uint32_t {
		if (off + 3 >= exif_end) return 0;
        return le ? (uint32_t)(jpg[off] | (jpg[off+1] << 8) | (jpg[off+2] << 16) | (jpg[off+3] << 24)) : (uint32_t)((jpg[off] << 24) | (jpg[off+1] << 16) | (jpg[off+2] << 8) | jpg[off+3]);
    };

    if (rd16(tiff + 2) != 0x002A) return std::nullopt;
    
	uint32_t ifd0_off = rd32(tiff + 4);
	
    size_t ifd = tiff + ifd0_off;
    if (ifd + 2 > exif_end) return std::nullopt;

    uint16_t count = rd16(ifd);
    ifd += 2;
	
    for (uint16_t i = 0; i < count; ++i) {
    	size_t entry = ifd + i * 12;
    	if (entry + 12 > exif_end) return std::nullopt;
        uint16_t tag = rd16(entry + 0);
        if (tag == 0x0112) {
        	return rd16(entry + 8); // 1..8 usually. 
        }
    }
    return std::nullopt;
}

static void rotate_rgb_180(std::vector<uint8_t>& rgb, int w, int h) {
	const int stride = w * 3;
	for (int y = 0; y < h / 2; ++y) {
    	int opp = h - 1 - y;
        for (int x = 0; x < w; ++x) {
        	for (int c = 0; c < 3; ++c)
                std::swap(rgb[y*stride + x*3 + c], rgb[opp*stride + (w-1-x)*3 + c]);
        }
    }
    if (h % 2 == 1) {
    	int y = h/2;
        for (int x = 0; x < w/2; ++x)
            for (int c = 0; c < 3; ++c)
                std::swap(rgb[y*stride + x*3 + c], rgb[y*stride + (w-1-x)*3 + c]);
    }
}

static void rotate_rgb_90cw(std::vector<uint8_t>& rgb, int& w, int& h) {
	std::vector<uint8_t> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    int nw = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
        	int nx = h - 1 - y, ny = x;
            for (int c = 0; c < 3; ++c)
                out[(static_cast<size_t>(ny) * nw + nx) * 3 + c] = rgb[(static_cast<size_t>(y) * w + x) * 3 + c];
        }
    }
    rgb.swap(out);
    std::swap(w, h);
}

static void rotate_rgb_270cw(std::vector<uint8_t>& rgb, int& w, int& h) {
    std::vector<uint8_t> out(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    int nw = h;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int nx = y, ny = w - 1 - x;
            for (int c = 0; c < 3; ++c)
                out[(static_cast<size_t>(ny) * nw + nx) * 3 + c] = rgb[(static_cast<size_t>(y) * w + x) * 3 + c];
        }
    }
    rgb.swap(out);
    std::swap(w, h);
}

// If exif_orientation found an Orientation tag, use normalize_orientation 
// and its above helpers to normalize the pixels, so that we can later safely remove
// the EXIF segment from the cover image and have correct orientation with viewers.
// Minimal mapper: handle 3,6,8 (most common). Add flips (2,4,5,7)...
static void normalize_orientation(std::vector<uint8_t>& rgb, int& w, int& h, int ori) {
	switch (ori) {
    	case 3: rotate_rgb_180(rgb, w, h); break;
        case 6: rotate_rgb_90cw(rgb, w, h); break;
        case 8: rotate_rgb_270cw(rgb, w, h); break;
        default: /* 1 or unsupported -> do nothing */ break;
    }
}

// Use Turbojpeg to re-encode JPG image and (if shouldDecreaseVals = true) use library stb_image_resize2, to resize image. 
static void resizeImage(std::vector<uint8_t>& image_file_vec, int quality_val, int decrease_dims_val, bool shouldDecreaseVals) {
	tjhandle decompressor = tjInitDecompress();
    if (!decompressor) {
    	throw std::runtime_error("tjInitDecompress() failed.");
    }	

    int width = 0, height = 0, jpegSubsamp = 0, jpegColorspace = 0;

    const unsigned char* JPG_IN = reinterpret_cast<const unsigned char*>(image_file_vec.data());

    if (tjDecompressHeader3(decompressor, JPG_IN, static_cast<unsigned long>(image_file_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
    	std::string err = tjGetErrorStr2(decompressor);
    	tjDestroy(decompressor);
    	throw std::runtime_error(std::string("tjDecompressHeader3: ") + err);
    }

    if (width < decrease_dims_val || height < decrease_dims_val) {
    	tjDestroy(decompressor);
        throw std::runtime_error("Image is too small to decrease by 1 pixel.");
    }
    
    const int 
    	PIXEL_FORMAT = TJPF_BGR,
		BYTES_PER_PIXEL = tjPixelSize[PIXEL_FORMAT];	

    std::vector<uint8_t> decoded_image_vec(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(BYTES_PER_PIXEL));

    if (tjDecompress2(decompressor, JPG_IN, static_cast<unsigned long>(image_file_vec.size()), reinterpret_cast<unsigned char*>(decoded_image_vec.data()), width, 0, height, PIXEL_FORMAT, 0) != 0) {
    	std::string err = tjGetErrorStr2(decompressor);
    	tjDestroy(decompressor);
    	throw std::runtime_error(std::string("tjDecompress2: ") + err);
    }

    tjDestroy(decompressor);
    
    // Correct image orientation if required.
    if (!shouldDecreaseVals) {
    	auto ori = exif_orientation(image_file_vec);
   		if (ori && *ori != 1) {
   			normalize_orientation(decoded_image_vec, width, height, *ori);
		}
   	}
   
    int newWidth = 0, newHeight = 0;
    std::vector<uint8_t> resized_image_vec;
    
    if (shouldDecreaseVals) {
    	newWidth  = width  - decrease_dims_val;
        newHeight = height - decrease_dims_val;
        
        std::cout << "\r" << std::string(44, ' ') << "\r"; 
    	std::cout << "Quality: " << (int)quality_val << "% | Width: " << newWidth << " | Height: " << newHeight << std::flush; 
    	
    	resized_image_vec.resize(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * static_cast<size_t>(BYTES_PER_PIXEL));

   		if (!stbir_resize_uint8_srgb(reinterpret_cast<unsigned char*>(decoded_image_vec.data()), width, height, 0, 
    		reinterpret_cast<unsigned char*>(resized_image_vec.data()), newWidth, newHeight, 0, static_cast<stbir_pixel_layout>(BYTES_PER_PIXEL))) {
    		throw std::runtime_error("stbir_resize_uint8_srgb failed.");
    	}	
    } else {
    	newWidth  = width;
        newHeight = height;
    }
   
    tjhandle compressor = tjInitCompress(); 
    if (!compressor) throw std::runtime_error("tjInitCompress() failed.");

    unsigned char* jpegBuf  = nullptr;
    unsigned long  jpegSize = 0;
    
    int 
    	subsamp = TJSAMP_444,
    	flags = TJFLAG_PROGRESSIVE | TJFLAG_ACCURATEDCT;
    
    unsigned char* vec = reinterpret_cast<unsigned char*>((shouldDecreaseVals ? resized_image_vec.data() : decoded_image_vec.data()));
    
    if (tjCompress2(compressor, vec, newWidth, 0, newHeight, PIXEL_FORMAT, &jpegBuf, &jpegSize, subsamp, quality_val, flags) != 0) {
    	if (jpegBuf) { tjFree(jpegBuf); jpegBuf = nullptr; }
    	std::string err = tjGetErrorStr2(compressor);
    	tjDestroy(compressor);
    	throw std::runtime_error(std::string("tjCompress2: ") + err);
    }
   
    std::vector<uint8_t> output_image_vec(jpegBuf, jpegBuf + jpegSize);
    
    tjDestroy(compressor);
    tjFree(jpegBuf);
    
    image_file_vec.swap(output_image_vec);
}

int main(int argc, char** argv) {
	try {
		auto args_opt = ProgramArgs::parse(argc, argv);
       	if (!args_opt) return 0; 
       		
		ProgramArgs args = *args_opt; 
		
		if (!fs::exists(args.image_file_path)) {
        	throw std::runtime_error("Image File Error: File not found.");
    	}
			
		if (!hasValidFilename(args.image_file_path)) {
    		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
		}

		if (!hasFileExtension(args.image_file_path, {".jpg", ".jpeg", ".jfif"})) {
        	throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
    	}	
    			
		std::ifstream image_file_ifs(args.image_file_path, std::ios::binary);
        	
    	if (!image_file_ifs) {
    		throw std::runtime_error("Read File Error: Unable to read image file. Check the filename and try again.");
   		}
		
		uintmax_t image_file_size = fs::file_size(args.image_file_path);

    	constexpr uint8_t MIN_IMAGE_SIZE = 134;

    	if (MIN_IMAGE_SIZE > image_file_size) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}

    	constexpr uintmax_t MAX_IMAGE_SIZE = 4 * 1024 * 1024;
    
    	if (image_file_size > MAX_IMAGE_SIZE) {
   			throw std::runtime_error("Image Size Error: Size of cover image exceeds maximum size limit.");
   		}
   		
   		std::vector<uint8_t> image_file_vec(image_file_size);
	
		image_file_ifs.read(reinterpret_cast<char*>(image_file_vec.data()), image_file_size);
		image_file_ifs.close();
	
		// Make sure JPG cover image has both "Start Of Image" & "End Of Image" markers.
		// Also, remove any trailing data after EOI marker.
		constexpr uint8_t 
			SOI0 = 0xFF, 
			SOI1 = 0xD8,
   			EOI0 = 0xFF, 
   			EOI1 = 0xD9;

	  	if (!(image_file_vec[0] == SOI0 && image_file_vec[1] == SOI1)) {
        	throw std::runtime_error("Image File Error: Missing SOI marker.");
    	}

    	const std::array<uint8_t,2> EOI {EOI0, EOI1};

    	auto last_eoi = std::find_end(image_file_vec.begin() + 2, image_file_vec.end(), EOI.begin(), EOI.end());
    	if (last_eoi == image_file_vec.end()) {
        	throw std::runtime_error("Image File Error: Missing EOI marker.");
    	}

    	auto after_eoi = last_eoi + 2;
    	if (after_eoi != image_file_vec.end()) {
        	image_file_vec.erase(after_eoi, image_file_vec.end());
    	}
		//---------
		
		constexpr uint8_t 
			COMPATIBLE_IMAGE_VAL = 0x19,
			JIFF_SIG_LENGTH = 20;

		bool 
			shouldDecreaseVals = false,
			isImageModified = false;
		
		std::vector<uint8_t>image_file_vec_copy;
		
		if  (image_file_vec[0x0D] != COMPATIBLE_IMAGE_VAL) {	

			int quality_val = 97, decrease_attempts = 300, decrease_dims_val = 0;

			std::cout << "\nChecking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n\n"
			  		<< "Image quality & dimensions will be reduced in an attempt to remove them.\n\n";

			resizeImage(image_file_vec, quality_val, decrease_dims_val, shouldDecreaseVals);
			
			// Save some more space:
			// Safely remove superfluous segments from cover image. (EXIF, ICC color profile, etc).
			auto eraseAppSegment = [](std::vector<uint8_t>& v, std::span<const uint8_t> sig) {
    			auto pos = searchSig(v, sig);
    			if (!pos) return;
    			if (*pos + 3 >= v.size()) return;

    			uint16_t block_len = (static_cast<uint16_t>(v[*pos + 2]) << 8) | static_cast<uint16_t>(v[*pos + 3]);
    			size_t erase_end = *pos + 2 + block_len;
    			if (erase_end > v.size()) return;

    			v.erase(v.begin() + *pos, v.begin() + erase_end);
			};
			
			constexpr std::array<uint8_t, 2>
				COMMENT_BLOCK_SIG 	{ 0x23, 0x3E },
				APP1_EXIF_SIG 		{ 0xFF, 0xE1 }, 
				APP2_ICC_SIG  		{ 0xFF, 0xE2 }; 

			constexpr std::array<uint8_t, 4>
				DQT1_SIG { 0xFF, 0xDB, 0x00, 0x43 },	// Define Quantization Tables SIG.
				DQT2_SIG { 0xFF, 0xDB, 0x00, 0x84 };
				
			eraseAppSegment(image_file_vec, std::span<const uint8_t>(APP1_EXIF_SIG));
			eraseAppSegment(image_file_vec, std::span<const uint8_t>(APP2_ICC_SIG));

    		auto dqt1 = searchSig(image_file_vec, std::span<const uint8_t>(DQT1_SIG));
    		auto dqt2 = searchSig(image_file_vec, std::span<const uint8_t>(DQT2_SIG));

			if (!dqt1 && !dqt2) {
    			throw std::runtime_error("Image File Error: No DQT segment found (corrupt or unsupported JPG).");
			}

			const size_t NPOS = static_cast<size_t>(-1);
			size_t dqt_pos = std::min(dqt1.value_or(NPOS), dqt2.value_or(NPOS));
			image_file_vec.erase(image_file_vec.begin(), image_file_vec.begin() + static_cast<std::ptrdiff_t>(dqt_pos));
			// ------------

			constexpr std::array<uint8_t, JIFF_SIG_LENGTH> JFIF_SIG	{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
	
			image_file_vec.insert(image_file_vec.begin(), JFIF_SIG.begin(), JFIF_SIG.end());

			image_file_size = image_file_vec.size();  // Get updated cover image size after image re-encode, removing superfluous segments & trailing data.
			
			image_file_vec_copy = image_file_vec;
	
			isImageModified = true;

			auto index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);
			
			shouldDecreaseVals = true;

			while(index_opt) {
				std::vector<uint8_t>().swap(image_file_vec);
				image_file_vec = image_file_vec_copy; 

				--decrease_attempts;
				++decrease_dims_val;
				quality_val -= (decrease_attempts % 15 == 0) ? 2 : 0;
				
				resizeImage(image_file_vec, quality_val, decrease_dims_val, shouldDecreaseVals);

				index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);
				
				if (!decrease_attempts) {
		  			std::cerr << "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.\n";
			  		throw std::runtime_error("Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.");
				}
			}
		}

		std::vector<uint8_t>().swap(image_file_vec_copy);

		if (shouldDecreaseVals) std::cout << '\n';

		constexpr std::array<uint8_t, 11>
			DEFAULT_BYTES 	{ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e },
			ALT_BYTES		{ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 }; 

		if (args.option == Option::Alt) {
			std::copy(ALT_BYTES.rbegin(), ALT_BYTES.rend(), image_file_vec.rbegin() + 2);
		} else {
			std::copy(DEFAULT_BYTES.rbegin(), DEFAULT_BYTES.rend(), image_file_vec.rbegin() + 2);
		}
	
		constexpr uint8_t PWSH_INSERT_INDEX = 6;
			
		if (!fs::exists(args.pwsh_file_path)) {
        	throw std::runtime_error("Script File Error: PowerShell script file not found.");
    	}
			
		if (!hasValidFilename(args.pwsh_file_path)) {
    		throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
		}

		if (!hasFileExtension(args.pwsh_file_path, {".ps1"})) {
        	throw std::runtime_error("File Type Error: Invalid script extension. Only expecting \".ps1\".");
    	}	
    			
		std::ifstream pwsh_file_ifs(args.pwsh_file_path, std::ios::binary);
        	
    	if (!pwsh_file_ifs) {
    		throw std::runtime_error("Read File Error: Unable to read image file. Check the filename and try again.");
   		}
		
		uintmax_t pwsh_file_size = fs::file_size(args.pwsh_file_path);
		
		constexpr uint16_t MAX_PWSH_SIZE = 10ULL * 1024;

    	constexpr uint8_t MIN_PWSH_SIZE = 10;
	
		if (MIN_PWSH_SIZE > pwsh_file_size) {
        	throw std::runtime_error("PowerShell File Error: Invalid file size.");
    	}
	
		if (pwsh_file_size > MAX_PWSH_SIZE) {
			throw std::runtime_error("PowerShell File Error: Size of PowerShell script exceeds maximum size limit.");
		}
	
		std::vector<uint8_t> pwsh_file_vec(pwsh_file_size);
	
		pwsh_file_ifs.read(reinterpret_cast<char*>(pwsh_file_vec.data()), pwsh_file_size);
		pwsh_file_ifs.close();
			
		constexpr std::array<uint8_t, 3> BOM_SIG { 0xEF, 0xBB, 0xBF };
	
		if (std::equal(BOM_SIG.begin(), BOM_SIG.end(), pwsh_file_vec.begin())) {
        	pwsh_file_vec.erase(pwsh_file_vec.begin(), pwsh_file_vec.begin() + 3);
        }		
	
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
	
		profile_vec.insert(profile_vec.end() - PWSH_INSERT_INDEX, pwsh_file_vec.begin(), pwsh_file_vec.end());

		std::vector<uint8_t>().swap(pwsh_file_vec);

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

		image_file_vec.insert(image_file_vec.begin() + JIFF_SIG_LENGTH , profile_vec.begin(), profile_vec.end());
		
		std::vector<uint8_t>().swap(profile_vec);

		while (bits) {
			image_file_vec[segment_size_field_index++] = (SEGMENT_SIZE >> (bits -= 8)) & 0xFF;
		}

		bits = 32;
	
		while (bits) {
			image_file_vec[profile_size_field_index++] = (PROFILE_SIZE >> (bits -= 8)) & 0xFF;
		}
	
		constexpr std::array<uint8_t, 6> JFIF_COMMENT_BLOCK {0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23};

		std::copy(JFIF_COMMENT_BLOCK.begin(), JFIF_COMMENT_BLOCK.end(), image_file_vec.begin() + jfif_comment_block_index);

		std::random_device rd;
    	std::mt19937 gen(rd());
    	std::uniform_int_distribution<> dist(10000, 99999);  // Five-digit random number

		const std::string OUTPUT_FILENAME = "jpws_" + std::to_string(dist(gen)) + ".jpg";

		std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

		if (!file_ofs) {
			throw std::runtime_error("Write Error: Unable to write to file.");
		}
	
		const uint32_t IMAGE_SIZE = static_cast<uint32_t>(image_file_vec.size());

		file_ofs.write(reinterpret_cast<const char*>(image_file_vec.data()), IMAGE_SIZE);
		file_ofs.close();
	
		std::vector<uint8_t>().swap(image_file_vec);
	
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

