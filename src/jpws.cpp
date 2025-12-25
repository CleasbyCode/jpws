// JPG-PowerShell Polyglot for X-Twitter (jpws v1.9) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024.

// CLI source code (Linux / Windows).

// Compile program (Linux):

// $ sudo apt install libturbojpeg0-dev

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
#include <array>
#include <bit>
#include <cstdint>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <optional>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {
    using Byte   = std::uint8_t;
    using vBytes = std::vector<Byte>;

    enum class FileTypeCheck : Byte {
        cover_image = 1, // Conceal mode...
        script_file = 2  // Conceal mode...
    };

    void displayInfo() {
        std::print(R"(

JPG-PowerShell Polyglot for X-Twitter (jpws v1.9)
Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024

CLI tool for embedding a PowerShell script within a JPG image,
creating a tweetable JPG-PowerShell polyglot file.

──────────────────────────
Compile & run (Linux)
──────────────────────────

$ sudo apt install libturbojpeg0-dev

$ chmod +x compile_jpws.sh
$ ./compile_jpws.sh

Compilation successful. Executable 'jpws' created.

$ sudo cp jpws /usr/bin
$ jpws

Usage:  jpws [-alt] <cover_image> <powershell_script>
        jpws --info

Share your "PowerShell-embedded" JPG image on X-Twitter.

Max script size is ~10KB.
Max image size is 4MB.

https://github.com/CleasbyCode/jpws

    )");
    }

    enum class Option : unsigned char { None, Alt };

    struct ProgramArgs {
        Option option{Option::None};
        fs::path image_file_path;
        fs::path pwsh_file_path;

        static std::optional<ProgramArgs> parse(int argc, char** argv);
    };

    std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) {
        auto arg = [&](int i) -> std::string_view {
            return (i >= 0 && i < argc) ? std::string_view(argv[i]) : std::string_view{};
        };

        constexpr std::string_view PREFIX = "Usage: ";
        const std::string
            PROG = fs::path(argv[0]).filename().string(),
            INDENT(PREFIX.size(), ' '),
            USAGE = std::format("{}{} <cover_image> <pwsh_script>\n""{}{} --info", PREFIX, PROG, INDENT, PROG, INDENT);

        auto die = [&]() -> std::optional<ProgramArgs> {
            throw std::runtime_error(USAGE);
        };

        if (argc < 2) die();

        if (argc == 2 && arg(1) == "--info") {
            displayInfo();
            return std::nullopt;
        }

        ProgramArgs out{};
        const std::string_view opt = arg(1);

        if (opt == "-alt") {
            if (argc != 4) die();
            if (arg(2).empty() || arg(3).empty()) die();
            out.image_file_path = fs::path(arg(2));
            out.pwsh_file_path  = fs::path(arg(3));
            out.option = Option::Alt;
        } else {
            if (argc != 3) die();
            if (arg(1).empty() || arg(2).empty()) die();
            out.image_file_path = fs::path(arg(1));
            out.pwsh_file_path  = fs::path(arg(2));
        }
        return out;
    }

    [[nodiscard]] bool hasValidFilename(const fs::path& p) {
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
        return std::ranges::all_of(filename, validChar);
    }

    [[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
        auto e = p.extension().string();
        std::ranges::transform(e, e.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return std::ranges::any_of(exts, [&e](std::string_view ext) {
            std::string c{ext};
            std::ranges::transform(c, c.begin(), [](unsigned char x) {
                return static_cast<char>(std::tolower(x));
            });
            return e == c;
        });
    }

    // Default limit of 0 means "Search Whole File".
    // Any other value means "Search ONLY up to this limit".
    [[nodiscard]] std::optional<std::size_t> searchSig(std::span<const Byte> v, std::span<const Byte> sig, std::size_t limit = 0) {
        auto search_span = (limit == 0 || limit > v.size())
            ? v
            : v.first(limit);

        auto it = std::ranges::search(search_span, sig);

        if (it.empty()) return std::nullopt;
        return static_cast<std::size_t>(it.begin() - v.begin());
    }

    [[nodiscard]] std::optional<uint16_t> exifOrientation(std::span<const Byte> jpg) {
        constexpr std::size_t EXIF_SEARCH_LIMIT = 4096;
        constexpr auto APP1_SIG = std::to_array<Byte>({0xFF, 0xE1});

        auto app1_pos_opt = searchSig(jpg, APP1_SIG, EXIF_SEARCH_LIMIT);

        if (!app1_pos_opt) return std::nullopt;
        std::size_t pos = *app1_pos_opt;

        if (pos + 4 > jpg.size()) return std::nullopt;

        uint16_t segment_length = (static_cast<uint16_t>(jpg[pos + 2]) << 8) | jpg[pos + 3];
        std::size_t exif_end = pos + 2 + segment_length;

        if (exif_end > jpg.size()) return std::nullopt;

        std::span<const Byte> payload(jpg.data() + pos + 4, segment_length - 2);

        constexpr std::size_t EXIF_HEADER_SIZE = 6;
        constexpr auto EXIF_SIG = std::to_array<Byte>({'E', 'x', 'i', 'f', '\0', '\0'});

        if (payload.size() < EXIF_HEADER_SIZE ||
            !std::ranges::equal(payload.first(EXIF_HEADER_SIZE), EXIF_SIG)) {
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
            uint16_t value;
            std::memcpy(&value, tiff_data.data() + offset, 2);
            return is_le ? value : std::byteswap(value);
        };

        auto read32 = [&](std::size_t offset) -> uint32_t {
            if (offset + 4 > tiff_data.size()) return 0;
            uint32_t value;
            std::memcpy(&value, tiff_data.data() + offset, 4);
            return is_le ? value : std::byteswap(value);
        };

        if (read16(2) != 0x002A) return std::nullopt;

        uint32_t ifd_offset = read32(4);

        if (ifd_offset < 8 || ifd_offset >= tiff_data.size()) return std::nullopt;

        uint16_t entry_count = read16(ifd_offset);
        std::size_t current_entry = ifd_offset + 2;

        constexpr uint16_t TAG_ORIENTATION = 0x0112;
        constexpr std::size_t ENTRY_SIZE = 12;

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
    [[nodiscard]] int getTransformOp(uint16_t orientation) {
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

        TJBuffer() = default;
        ~TJBuffer() { if (data) tjFree(data); }

        TJBuffer(const TJBuffer&) = delete;
        TJBuffer& operator=(const TJBuffer&) = delete;
        TJBuffer(TJBuffer&&) = delete;
        TJBuffer& operator=(TJBuffer&&) = delete;
    };

    void optimizeImage(vBytes& jpg_vec) {
        if (jpg_vec.empty()) {
            throw std::runtime_error("JPG image is empty!");
        }

        TJHandle transformer;
        transformer.handle = tjInitTransform();
        if (!transformer) {
            throw std::runtime_error("tjInitTransform() failed");
        }

        int width = 0, height = 0, jpegSubsamp = 0, jpegColorspace = 0;
        if (tjDecompressHeader3(transformer.get(), jpg_vec.data(), static_cast<unsigned long>(jpg_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
            throw std::runtime_error(std::format("Image Error: {}", tjGetErrorStr2(transformer.get())));
        }

        if (width < 400 || height < 400) {
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
            throw std::runtime_error(std::format("Image Error: {}", tjGetErrorStr2(transformer.get())));
        }

        jpg_vec.assign(dstBuffer.data, dstBuffer.data + dstSize);
    }

    void resizeImage(vBytes& image_file_vec, int quality_val, int decrease_dims_val) {
        TJHandle decompressor;
        decompressor.handle = tjInitDecompress();
        if (!decompressor) {
            throw std::runtime_error("tjInitDecompress() failed.");
        }

        int width = 0, height = 0, jpegSubsamp = 0, jpegColorspace = 0;
        const unsigned char* JPG_IN = reinterpret_cast<const unsigned char*>(image_file_vec.data());

        if (tjDecompressHeader3(decompressor.get(), JPG_IN, static_cast<unsigned long>(image_file_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
            throw std::runtime_error(std::format("tjDecompressHeader3: {}", tjGetErrorStr2(decompressor.get())));
        }

        if (width < decrease_dims_val || height < decrease_dims_val) {
            throw std::runtime_error("Image is too small to decrease by 1 pixel.");
        }

        const int
            PIXEL_FORMAT    = TJPF_XBGR,
            BYTES_PER_PIXEL = tjPixelSize[PIXEL_FORMAT];

        vBytes decoded_image_vec(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(BYTES_PER_PIXEL));

        if (tjDecompress2(decompressor.get(), JPG_IN, static_cast<unsigned long>(image_file_vec.size()), decoded_image_vec.data(), width, 0, height, PIXEL_FORMAT, 0) != 0) {
            throw std::runtime_error(std::format("tjDecompress2: {}", tjGetErrorStr2(decompressor.get())));
        }

        decompressor.reset();

        int
            newWidth  = width  - decrease_dims_val,
            newHeight = height - decrease_dims_val;

        std::print("\r{:44}\r", "");
        std::print("Quality: {}% | Width: {} | Height: {}", quality_val, newWidth, newHeight);
        std::fflush(stdout);

        vBytes resized_image_vec(static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight) * static_cast<std::size_t>(BYTES_PER_PIXEL));

        if (!stbir_resize_uint8_srgb(decoded_image_vec.data(), width, height, 0, resized_image_vec.data(), newWidth, newHeight, 0, static_cast<stbir_pixel_layout>(BYTES_PER_PIXEL))) {
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
            flags   = TJFLAG_PROGRESSIVE;

        if (tjCompress2(compressor.get(), resized_image_vec.data(), newWidth, 0, newHeight, PIXEL_FORMAT, &jpegBuf.data, &jpegSize, subsamp, quality_val, flags) != 0) {
            throw std::runtime_error(std::format("tjCompress2: {}", tjGetErrorStr2(compressor.get())));
        }

        image_file_vec.assign(jpegBuf.data, jpegBuf.data + jpegSize);
    }

    [[nodiscard]] vBytes readFile(const fs::path& path, FileTypeCheck FileType = FileTypeCheck::script_file) {
        if (!fs::exists(path) || !fs::is_regular_file(path)) {
            throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
        }

        std::size_t file_size = fs::file_size(path);

        if (!file_size) {
            throw std::runtime_error("Error: File is empty.");
        }

        if (FileType == FileTypeCheck::cover_image) {
            constexpr std::size_t
                MINIMUM_IMAGE_SIZE = 134,
                MAX_IMAGE_SIZE 	   = 5 * 1024 * 1024;

            if (!hasFileExtension(path, {".jpg", ".jpeg", ".jfif"})) {
                throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
            }
            if (MINIMUM_IMAGE_SIZE > file_size) {
                throw std::runtime_error("File Error: Invalid image file size.");
            }

            if (file_size > MAX_IMAGE_SIZE) {
                throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
            }
        }

        if (FileType == FileTypeCheck::script_file) {
            constexpr std::size_t
                MAX_PWSH_SIZE = 10 * 1024,
                MIN_PWSH_SIZE = 10;

            if (MIN_PWSH_SIZE > file_size) {
                throw std::runtime_error("Script File Error: PowerShell script is below minimum size.");
            }

            if (file_size > MAX_PWSH_SIZE) {
                throw std::runtime_error("Script File Error: PowerShell script exceeds maximum size limit.");
            }
            if (!hasFileExtension(path, {".ps1"})) {
                throw std::runtime_error("File Type Error: Invalid script extension. Only expecting \".ps1\".");
            }
        }

        if (!hasValidFilename(path)) {
            throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
        }

        std::ifstream file(path, std::ios::binary);

        if (!file) {
            throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
        }

        vBytes vec(file_size);
        file.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(file_size));

        if (file.gcount() != static_cast<std::streamsize>(file_size)) {
            throw std::runtime_error("Failed to read full file: partial read");
        }
        return vec;
    }
}

int main(int argc, char** argv) {
    try {
        auto args_opt = ProgramArgs::parse(argc, argv);
        if (!args_opt) return 0;

        ProgramArgs args = *args_opt;

		vBytes image_file_vec = readFile(args.image_file_path, FileTypeCheck::cover_image);

        constexpr Byte COMPATIBLE_IMAGE_VAL = 0x19;  // Compatible image value marker.

        constexpr std::size_t JFIF_SIG_LENGTH = 20;

        bool
            shouldDecreaseVals = false,
            isImageModified    = false;

        vBytes image_file_vec_copy;

        constexpr std::size_t MARKER_INDEX = 0x0D;

        if (image_file_vec[MARKER_INDEX] != COMPATIBLE_IMAGE_VAL) {
            int quality_val = 97, decrease_attempts = 300, decrease_dims_val = 0;

            std::println("\nChecking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n");
            std::println("Image quality & dimensions will be reduced in an attempt to remove them.\n");

            optimizeImage(image_file_vec);

            constexpr std::size_t DQT_SEARCH_LIMIT = 100;

            constexpr auto COMMENT_BLOCK_SIG = std::to_array<Byte>({ 0x23, 0x3E });

            constexpr auto
                DQT1_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x43 }),
                DQT2_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x84 });

            auto
                dqt1 = searchSig(image_file_vec, DQT1_SIG, DQT_SEARCH_LIMIT),
                dqt2 = searchSig(image_file_vec, DQT2_SIG, DQT_SEARCH_LIMIT);

            if (!dqt1 && !dqt2) {
                throw std::runtime_error("Image File Error: No DQT segment found (corrupt or unsupported JPG).");
            }

            constexpr std::size_t NPOS = static_cast<std::size_t>(-1);

            std::size_t dqt_pos = std::min(dqt1.value_or(NPOS), dqt2.value_or(NPOS));

            image_file_vec.erase(image_file_vec.begin(), image_file_vec.begin() + static_cast<std::ptrdiff_t>(dqt_pos));

            constexpr auto JFIF_SIG = std::to_array<Byte>({ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 });

            image_file_vec.insert(image_file_vec.begin(), JFIF_SIG.begin(), JFIF_SIG.end());
            image_file_vec_copy = image_file_vec;

            isImageModified = true;

            auto index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);

            shouldDecreaseVals = true;

            while (index_opt) {
                image_file_vec.clear();
                image_file_vec = image_file_vec_copy;

                --decrease_attempts;
                ++decrease_dims_val;
                quality_val -= (decrease_attempts % 15 == 0) ? 2 : 0;

                resizeImage(image_file_vec, quality_val, decrease_dims_val);

                index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);

                if (!decrease_attempts) {
                    std::println(std::cerr, "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.");
                    throw std::runtime_error("Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.");
                }
            }
        }

		vBytes().swap(image_file_vec_copy);

        if (shouldDecreaseVals) std::println("");

        constexpr auto
            DEFAULT_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e }),
            ALT_BYTES     = std::to_array<Byte>({ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 });

        if (args.option == Option::Alt) {
            std::ranges::copy(ALT_BYTES | std::views::reverse, image_file_vec.rbegin() + 2);
        } else {
            std::ranges::copy(DEFAULT_BYTES | std::views::reverse, image_file_vec.rbegin() + 2);
        }

		vBytes pwsh_file_vec = readFile(args.pwsh_file_path, FileTypeCheck::script_file);

        constexpr auto BOM_SIG = std::to_array<Byte>({ 0xEF, 0xBB, 0xBF });

        if (std::ranges::equal(pwsh_file_vec | std::views::take(3), BOM_SIG)) {
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

		constexpr std::size_t PWSH_INSERT_INDEX = 6;
        profile_vec.insert(profile_vec.end() - PWSH_INSERT_INDEX, pwsh_file_vec.begin(), pwsh_file_vec.end());

		vBytes().swap(pwsh_file_vec);

        constexpr std::size_t
            JFIF_COMMENT_BLOCK_INDEX = 0x0C,
            SEGMENT_SIZE_FIELD_INDEX = 0x16,
            PROFILE_SIZE_FIELD_INDEX = 0x26;

        std::size_t
            segment_index = SEGMENT_SIZE_FIELD_INDEX,
            profile_index = PROFILE_SIZE_FIELD_INDEX;

        const std::size_t
            SEGMENT_SIZE = (profile_vec.size() + JFIF_SIG_LENGTH) - segment_index,
            PROFILE_SIZE = SEGMENT_SIZE - 16;

        constexpr std::size_t MAX_POWERSHELL_FILE_SIZE = 10 * 1024;

        if (SEGMENT_SIZE > MAX_POWERSHELL_FILE_SIZE) {
            throw std::runtime_error("Segment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.");
        }

        image_file_vec.insert(image_file_vec.begin() + JFIF_SIG_LENGTH, profile_vec.begin(), profile_vec.end());

		vBytes().swap(profile_vec);

        auto writeBigEndian = [](vBytes& vec, std::size_t index, std::size_t value, int bytes) {
            for (int i = bytes - 1; i >= 0; --i) {
                vec[index++] = (value >> (i * 8)) & 0xFF;
            }
        };

        writeBigEndian(image_file_vec, segment_index, SEGMENT_SIZE, 2);
        writeBigEndian(image_file_vec, profile_index, PROFILE_SIZE, 4);

        constexpr auto JFIF_COMMENT_BLOCK = std::to_array<Byte>({ 0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23 });

        std::ranges::copy(JFIF_COMMENT_BLOCK, image_file_vec.begin() + JFIF_COMMENT_BLOCK_INDEX);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(10000, 99999);

        const std::string OUTPUT_FILENAME = std::format("jpws_{}.jpg", dist(gen));

        std::ofstream file_ofs(OUTPUT_FILENAME, std::ios::binary);

        if (!file_ofs) {
            throw std::runtime_error("Write Error: Unable to write to file.");
        }

        const std::size_t IMAGE_SIZE = image_file_vec.size();

        file_ofs.write(reinterpret_cast<const char*>(image_file_vec.data()), IMAGE_SIZE);
        file_ofs.close();

		vBytes().swap(image_file_vec);

        std::println("\nSaved JPG-PowerShell polyglot image: {} ({} bytes).", OUTPUT_FILENAME, IMAGE_SIZE);

        if (isImageModified) {
            std::println("\nComment-block close sequences successfully removed from image.");
            std::println("\nPlease check to make sure size & quality of cover image is acceptable.");
        }
        std::println("\nComplete!\n");
        return 0;
    }
    catch (const std::runtime_error& e) {
        std::println(std::cerr, "\n{}\n", e.what());
        return 1;
    }
}
