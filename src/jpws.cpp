// JPG-PowerShell Polyglot for X-Twitter (jpws v1.7) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024.

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

using Byte   = std::uint8_t;
using vBytes = std::vector<Byte>;

static void displayInfo() {
	std::cout << R"(

JPG-PowerShell Polyglot for X-Twitter (jpws v1.7)
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
    
    static std::optional<ProgramArgs> parse(int argc, char** argv); 
};

std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) { 
    using std::string_view;
    auto arg = [&](int i) -> string_view {
        return (i >= 0 && i < argc) ? string_view(argv[i]) : string_view{};
    };
    const std::string prog = fs::path(argv[0]).filename().string();
    const std::string USAGE =
        "Usage: " + prog + " [-alt] <cover_image> <powershell_script>\n\t\b"
            + prog + " --info";
        
    auto die = [&]() -> std::optional<ProgramArgs> {
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
        if (argc != 4) die();
        out.image_file_path = fs::path(arg(2));
        out.pwsh_file_path  = fs::path(arg(3));
        out.option = Option::Alt;
    } else {
        if (argc != 3) die();
        out.image_file_path = fs::path(arg(1));
        out.pwsh_file_path  = fs::path(arg(2));
    }
    return out;
}

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

// Default limit of 0 means "Search Whole File". 
// Any other value means "Search ONLY up to this limit".
static std::optional<std::size_t> searchSig(const vBytes& v, std::span<const Byte> sig, std::size_t limit = 0) {   
	auto end_it = (limit == 0 || limit > v.size()) 
    	? v.end() 
    	: v.begin() + limit;

    auto it = std::search(v.begin(), end_it, sig.begin(), sig.end());
    
    if (it == end_it) return std::nullopt;
    return static_cast<std::size_t>(it - v.begin());
}

[[nodiscard]] static std::optional<uint16_t> exifOrientation(const vBytes& jpg) {
	constexpr size_t EXIF_SEARCH_LIMIT = 4096ULL;
	constexpr auto APP1_SIG = std::to_array<Byte>({0xFF, 0xE1});

	auto app1_pos_opt = searchSig(jpg, std::span<const Byte>(APP1_SIG), EXIF_SEARCH_LIMIT);

    if (!app1_pos_opt) return std::nullopt;
    std::size_t pos = *app1_pos_opt;

    if (pos + 4 > jpg.size()) return std::nullopt;

    uint16_t segment_length = (static_cast<uint16_t>(jpg[pos + 2]) << 8) | jpg[pos + 3];
    std::size_t exif_end = pos + 2 + segment_length;

    if (exif_end > jpg.size()) return std::nullopt;

    std::span<const Byte> payload(jpg.data() + pos + 4, segment_length - 2);

    constexpr std::size_t EXIF_HEADER_SIZE = 6ULL;
    constexpr auto EXIF_SIG = std::to_array<Byte>({'E', 'x', 'i', 'f', '\0', '\0'});

    if (payload.size() < EXIF_HEADER_SIZE || 
    	std::memcmp(payload.data(), EXIF_SIG.data(), EXIF_HEADER_SIZE) != 0) {
        return std::nullopt;
    }
    
    std::span<const Byte> tiff_data = payload.subspan(EXIF_HEADER_SIZE);
    
    if (tiff_data.size() < 8) return std::nullopt; 

    bool is_le = false;
    if (tiff_data[0] == 'I' && tiff_data[1] == 'I') is_le = true;      
    else if (tiff_data[0] == 'M' && tiff_data[1] == 'M') is_le = false;
    else return std::nullopt;

    auto read16 = [&](std::size_t offset) -> uint16_t {
    	if (offset + 2 > tiff_data.size()) return 0;
        return is_le ? 
            static_cast<uint16_t>(tiff_data[offset] | (tiff_data[offset + 1] << 8)) :
            static_cast<uint16_t>((tiff_data[offset] << 8) | tiff_data[offset + 1]);
    };

    auto read32 = [&](std::size_t offset) -> uint32_t {
        if (offset + 4 > tiff_data.size()) return 0;
        if (is_le) {
            return static_cast<uint32_t>(tiff_data[offset]) | 
                   (static_cast<uint32_t>(tiff_data[offset + 1]) << 8) | 
                   (static_cast<uint32_t>(tiff_data[offset + 2]) << 16) | 
                   (static_cast<uint32_t>(tiff_data[offset + 3]) << 24);
        } else {
            return (static_cast<uint32_t>(tiff_data[offset]) << 24) | 
                   (static_cast<uint32_t>(tiff_data[offset + 1]) << 16) | 
                   (static_cast<uint32_t>(tiff_data[offset + 2]) << 8) | 
                   static_cast<uint32_t>(tiff_data[offset + 3]);
        }
    };

    if (read16(2) != 0x002A) return std::nullopt;

    uint32_t ifd_offset = read32(4);
    
    if (ifd_offset < 8 || ifd_offset >= tiff_data.size()) return std::nullopt;
    
    uint16_t entry_count = read16(ifd_offset);
    std::size_t current_entry = ifd_offset + 2ULL; 

    constexpr uint16_t TAG_ORIENTATION = 0x0112;
    constexpr std::size_t ENTRY_SIZE = 12ULL;

    for (uint16_t i = 0; i < entry_count; ++i) {
    	if (current_entry + ENTRY_SIZE > tiff_data.size()) return std::nullopt;

        uint16_t tag_id = read16(current_entry);
        
        if (tag_id == TAG_ORIENTATION) {
            return read16(current_entry + 8);
        }
        current_entry += ENTRY_SIZE;
    }
    return std::nullopt;
}

// Helper: Map EXIF orientation (1-8) to TurboJPEG Transform Operations
static int getTransformOp(uint16_t orientation) {
    switch (orientation) {
        case 2: return TJXOP_HFLIP;
        case 3: return TJXOP_ROT180;
        case 4: return TJXOP_VFLIP;
        case 5: return TJXOP_TRANSPOSE;
        case 6: return TJXOP_ROT90;
        case 7: return TJXOP_TRANSVERSE;
        case 8: return TJXOP_ROT270;
        default: return TJXOP_NONE;
    }
}

// TurboJPEG. RAII wrapper for tjhandle (decompressor or compressor)
struct TJHandle {
	tjhandle handle = nullptr;

    TJHandle() = default;

    TJHandle(const TJHandle&) = delete;
   	TJHandle& operator=(const TJHandle&) = delete;

    TJHandle(TJHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }
    
    TJHandle& operator=(TJHandle&& other) noexcept {
    	if (this != &other) {
        	reset();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    ~TJHandle() {
        reset();
    }

    void reset() {
        if (handle) {
        	tjDestroy(handle);
            handle = nullptr;
        }
    }

    tjhandle get() const { return handle; }
    tjhandle operator->() const { return handle; }
    explicit operator bool() const { return handle != nullptr; }
};

struct TJBuffer {
	unsigned char* data = nullptr;
	~TJBuffer() { if (data) tjFree(data); }
};

static void optimizeImage(vBytes& jpg_vec) {
	if (jpg_vec.empty()) {
        throw std::runtime_error("JPG image is empty!");
    }

    TJHandle transformer;
    transformer.handle = tjInitTransform();
    if (!transformer.handle) {
        throw std::runtime_error("tjInitTransform() failed");
    }
  
    int width = 0, height = 0, jpegSubsamp = 0, jpegColorspace = 0;
    if (tjDecompressHeader3(transformer.get(), jpg_vec.data(), static_cast<unsigned long>(jpg_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
        throw std::runtime_error(std::string("Image Error: ") + tjGetErrorStr2(transformer.get()));
    }

	if (width < 300 && height < 300) {
        throw std::runtime_error("Image Error: Dimensions are too small.\nFor platform compatibility, cover image must be at least 300px for both width and height.");
    }

    auto ori_opt = exifOrientation(jpg_vec);
    int xop = TJXOP_NONE;
    
    if (ori_opt) {
    	xop = getTransformOp(*ori_opt);
    }

    tjtransform xform;
    std::memset(&xform, 0, sizeof(tjtransform));
    xform.op = xop;
   
    xform.options = TJXOPT_COPYNONE | TJXOPT_TRIM | TJXOPT_PROGRESSIVE;
	
    TJBuffer dstBuffer; 
    unsigned long dstSize = 0;

    if (tjTransform(transformer.get(), jpg_vec.data(), static_cast<unsigned long>(jpg_vec.size()), 1, &dstBuffer.data, &dstSize, &xform, 0) != 0) {
    	throw std::runtime_error(std::string("tjTransform: ") + tjGetErrorStr2(transformer.get()));
    }

    if (xop == TJXOP_ROT90 || xop == TJXOP_ROT270 || xop == TJXOP_TRANSPOSE || xop == TJXOP_TRANSVERSE) {
        std::swap(width, height);
    }
    jpg_vec.assign(dstBuffer.data, dstBuffer.data + dstSize);
}

static void resizeImage(std::vector<uint8_t>& image_file_vec, int quality_val, int decrease_dims_val) {
    TJHandle decompressor;
    decompressor.handle = tjInitDecompress();
    if (!decompressor) {
        throw std::runtime_error("tjInitDecompress() failed.");
    }

    int width = 0, height = 0, jpegSubsamp = 0, jpegColorspace = 0;
    const unsigned char* JPG_IN = reinterpret_cast<const unsigned char*>(image_file_vec.data());

    if (tjDecompressHeader3(decompressor.get(), JPG_IN, static_cast<unsigned long>(image_file_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
        throw std::runtime_error(std::string("tjDecompressHeader3: ") + tjGetErrorStr2(decompressor.get()));
    }

    if (width < decrease_dims_val || height < decrease_dims_val) {
        throw std::runtime_error("Image is too small to decrease by 1 pixel.");
    }

    const int 
        PIXEL_FORMAT 	= TJPF_XBGR,
        BYTES_PER_PIXEL = tjPixelSize[PIXEL_FORMAT];

    std::vector<uint8_t> decoded_image_vec(static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(BYTES_PER_PIXEL));

    if (tjDecompress2(decompressor.get(), JPG_IN, static_cast<unsigned long>(image_file_vec.size()), decoded_image_vec.data(), width, 0, height, PIXEL_FORMAT, 0) != 0) {
        throw std::runtime_error(std::string("tjDecompress2: ") + tjGetErrorStr2(decompressor.get()));
    }

    decompressor.reset(); // Done with decompressor

    int 
		newWidth  = width  - decrease_dims_val,
    	newHeight = height - decrease_dims_val;

    std::cout << "\r" << std::string(44, ' ') << "\r"; 
    std::cout << "Quality: " << quality_val << "% | Width: " << newWidth << " | Height: " << newHeight << std::flush;

    std::vector<uint8_t> resized_image_vec(static_cast<size_t>(newWidth) * static_cast<size_t>(newHeight) * static_cast<size_t>(BYTES_PER_PIXEL));

    if (!stbir_resize_uint8_srgb(decoded_image_vec.data(), width, height, 0, 
    	resized_image_vec.data(), newWidth, newHeight, 0, static_cast<stbir_pixel_layout>(BYTES_PER_PIXEL))) {
        throw std::runtime_error("stbir_resize_uint8_srgb failed.");
    }

    TJHandle compressor;
    compressor.handle = tjInitCompress();
    if (!compressor) {
        throw std::runtime_error("tjInitCompress() failed.");
    }

    TJBuffer jpegBuf;
    unsigned long jpegSize = 0;

    int 
		subsamp = TJSAMP_440,
    	flags 	= TJFLAG_PROGRESSIVE | TJFLAG_ACCURATEDCT;

    if (tjCompress2(compressor.get(), resized_image_vec.data(), newWidth, 0, newHeight, PIXEL_FORMAT, &jpegBuf.data, &jpegSize, subsamp, quality_val, flags) != 0) {
        throw std::runtime_error(std::string("tjCompress2: ") + tjGetErrorStr2(compressor.get()));
    }
    image_file_vec.assign(jpegBuf.data, jpegBuf.data + jpegSize);
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
		
		std::size_t image_file_size = fs::file_size(args.image_file_path);

    	constexpr std::size_t 
    		MIN_IMAGE_SIZE = 134ULL,
    		MAX_IMAGE_SIZE = 4ULL * 1024 * 1024;

    	if (MIN_IMAGE_SIZE > image_file_size) {
        	throw std::runtime_error("Image File Error: Invalid file size.");
    	}
    
    	if (image_file_size > MAX_IMAGE_SIZE) {
   			throw std::runtime_error("Image Size Error: Size of cover image exceeds maximum size limit.");
   		}
   		
   		std::vector<uint8_t> image_file_vec(image_file_size);
	
		image_file_ifs.read(reinterpret_cast<char*>(image_file_vec.data()), image_file_size);
		image_file_ifs.close();

		constexpr uint8_t COMPATIBLE_IMAGE_VAL = 0x19;
		
		constexpr std::size_t JIFF_SIG_LENGTH = 20ULL;

		bool 
			shouldDecreaseVals = false,
			isImageModified    = false;
		
		std::vector<uint8_t>image_file_vec_copy;
		
		if  (image_file_vec[0x0D] != COMPATIBLE_IMAGE_VAL) {	

			int quality_val = 97, decrease_attempts = 300, decrease_dims_val = 0;

			std::cout << "\nChecking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n\n"
			  		<< "Image quality & dimensions will be reduced in an attempt to remove them.\n\n";

			optimizeImage(image_file_vec);
			
			constexpr size_t DQT_SEARCH_LIMIT = 100ULL;   
          
    		constexpr auto COMMENT_BLOCK_SIG = std::to_array<Byte>({ 0x23, 0x3E });
    			 
    		constexpr auto 
    			DQT1_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x43 }),    
        		DQT2_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x84 });
                
    		auto 
    			dqt1 = searchSig(image_file_vec, std::span<const Byte>(DQT1_SIG), DQT_SEARCH_LIMIT),
        		dqt2 = searchSig(image_file_vec, std::span<const Byte>(DQT2_SIG), DQT_SEARCH_LIMIT);

    		if (!dqt1 && !dqt2) {
    			throw std::runtime_error("Image File Error: No DQT segment found (corrupt or unsupported JPG).");
   			}

    		const std::size_t NPOS = static_cast<std::size_t>(-1);
            
    		std::size_t dqt_pos = std::min(dqt1.value_or(NPOS), dqt2.value_or(NPOS));
            
    		// Erase everything before DQT.
    		// This leaves the cover image with NO Start of Image (SOI) marker. 
    		// We will write this back later... 
	
    		image_file_vec.erase(image_file_vec.begin(), image_file_vec.begin() + static_cast<std::ptrdiff_t>(dqt_pos));

			constexpr auto JFIF_SIG	= std::to_array<Byte>({ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 });
	
			image_file_vec.insert(image_file_vec.begin(), JFIF_SIG.begin(), JFIF_SIG.end());

			image_file_size = image_file_vec.size();  // Get updated cover image size after image re-encode, removing superfluous segments & trailing data.
			
			image_file_vec_copy = image_file_vec;
	
			isImageModified = true;

			auto index_opt = searchSig(image_file_vec, std::span<const Byte>(COMMENT_BLOCK_SIG));
			
			shouldDecreaseVals = true;

			while(index_opt) {
				std::vector<uint8_t>().swap(image_file_vec);
				image_file_vec = image_file_vec_copy; 

				--decrease_attempts;
				++decrease_dims_val;
				quality_val -= (decrease_attempts % 15 == 0) ? 2 : 0;
				
				resizeImage(image_file_vec, quality_val, decrease_dims_val);

				index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);
				
				if (!decrease_attempts) {
		  			std::cerr << "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.\n";
			  		throw std::runtime_error("Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.");
				}
			}
		}

		std::vector<uint8_t>().swap(image_file_vec_copy);

		if (shouldDecreaseVals) std::cout << '\n';

		constexpr auto 
			DEFAULT_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e }),
			ALT_BYTES 	  = std::to_array<Byte>({ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 }); 

		if (args.option == Option::Alt) {
			std::copy(ALT_BYTES.rbegin(), ALT_BYTES.rend(), image_file_vec.rbegin() + 2);
		} else {
			std::copy(DEFAULT_BYTES.rbegin(), DEFAULT_BYTES.rend(), image_file_vec.rbegin() + 2);
		}
	
		constexpr std::size_t PWSH_INSERT_INDEX = 6ULL;
			
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
		
		constexpr std::size_t 
			MAX_PWSH_SIZE = 10ULL * 1024,
			MIN_PWSH_SIZE = 10ULL;
	
		if (MIN_PWSH_SIZE > pwsh_file_size) {
        	throw std::runtime_error("PowerShell File Error: Invalid file size.");
    	}
	
		if (pwsh_file_size > MAX_PWSH_SIZE) {
			throw std::runtime_error("PowerShell File Error: Size of PowerShell script exceeds maximum size limit.");
		}
	
		std::vector<uint8_t> pwsh_file_vec(pwsh_file_size);
	
		pwsh_file_ifs.read(reinterpret_cast<char*>(pwsh_file_vec.data()), pwsh_file_size);
		pwsh_file_ifs.close();
			
		constexpr auto BOM_SIG = std::to_array<Byte>({ 0xEF, 0xBB, 0xBF });
	
		if (std::equal(BOM_SIG.begin(), BOM_SIG.end(), pwsh_file_vec.begin())) {
        	pwsh_file_vec.erase(pwsh_file_vec.begin(), pwsh_file_vec.begin() + 3);
        }		
	
		vBytes profile_vec = { 
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

		vBytes().swap(pwsh_file_vec);

		Byte bits = 16;
		
		std::size_t
			jfif_comment_block_index = 0x0C,					
			segment_size_field_index = 0x16,
			profile_size_field_index = 0x26;		
		
		const size_t 
			SEGMENT_SIZE = (profile_vec.size() + JIFF_SIG_LENGTH) - segment_size_field_index,
			PROFILE_SIZE = SEGMENT_SIZE - bits;
	
		constexpr std::size_t MAX_POWERSHELL_FILE_SIZE = 10ULL * 1024; 

		if (SEGMENT_SIZE > MAX_POWERSHELL_FILE_SIZE) {
			throw std::runtime_error("Segment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.");
		}

		image_file_vec.insert(image_file_vec.begin() + JIFF_SIG_LENGTH , profile_vec.begin(), profile_vec.end());
		
		vBytes().swap(profile_vec);

		while (bits) {
			image_file_vec[segment_size_field_index++] = (SEGMENT_SIZE >> (bits -= 8)) & 0xFF;
		}

		bits = 32;
	
		while (bits) {
			image_file_vec[profile_size_field_index++] = (PROFILE_SIZE >> (bits -= 8)) & 0xFF;
		}
	
		constexpr auto JFIF_COMMENT_BLOCK = std::to_array<Byte>({0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23});

		std::copy(JFIF_COMMENT_BLOCK.begin(), JFIF_COMMENT_BLOCK.end(), image_file_vec.begin() + jfif_comment_block_index);

		std::random_device rd;
    	std::mt19937 gen(rd());
    	std::uniform_int_distribution<> dist(10000, 99999);  

		const std::string OUTPUT_FILENAME = "jpws_" + std::to_string(dist(gen)) + ".jpg";

		std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

		if (!file_ofs) {
			throw std::runtime_error("Write Error: Unable to write to file.");
		}
	
		const std::size_t IMAGE_SIZE = image_file_vec.size();

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

