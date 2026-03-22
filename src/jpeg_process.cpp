#define STB_IMAGE_RESIZE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "stb_image/include/stb_image_resize2.h"
#pragma GCC diagnostic pop

#include "jpeg_process.h"

#include <turbojpeg.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>

namespace {
    constexpr int MIN_COVER_IMAGE_DIMENSION = 400;
    constexpr int MAX_COVER_IMAGE_DIMENSION = 8'192;
    constexpr std::size_t MAX_COVER_IMAGE_PIXELS = 25'000'000;

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

    void validateImageDimensions(int width, int height) {
        if (width <= 0 || height <= 0) {
            throw std::runtime_error("Image Error: Invalid image dimensions.");
        }

        if (width < MIN_COVER_IMAGE_DIMENSION || height < MIN_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error("Image Error: Dimensions are too small.\nFor platform compatibility, cover image must be at least 400px for both width and height.");
        }

        if (width > MAX_COVER_IMAGE_DIMENSION || height > MAX_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error(std::format("Image Error: Dimensions exceed the supported maximum of {}px.", MAX_COVER_IMAGE_DIMENSION));
        }

        const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        if (pixel_count > MAX_COVER_IMAGE_PIXELS) {
            throw std::runtime_error("Image Error: Pixel count exceeds the supported maximum of 25 megapixels.");
        }
    }

    [[nodiscard]] std::size_t checkedPixelBufferSize(int width, int height, int bytes_per_pixel) {
        if (width <= 0 || height <= 0) {
            throw std::runtime_error("Image Error: Invalid image dimensions.");
        }

        if (bytes_per_pixel <= 0) {
            throw std::runtime_error("Image Error: Invalid pixel format.");
        }

        const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        if (pixel_count > MAX_COVER_IMAGE_PIXELS) {
            throw std::runtime_error("Image Error: Pixel count exceeds the supported maximum of 25 megapixels.");
        }

        if (pixel_count > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(bytes_per_pixel)) {
            throw std::runtime_error("Image dimensions too large for pixel buffer allocation.");
        }

        return pixel_count * static_cast<std::size_t>(bytes_per_pixel);
    }

    [[nodiscard]] bool hasCompatibleJfifHeader(std::span<const Byte> jpg) {
        constexpr auto COMPATIBLE_JFIF_SIG = std::to_array<Byte>({
            0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46,
            0x00, 0x01, 0x01, 0x19, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00
        });

        return jpg.size() >= COMPATIBLE_JFIF_SIG.size() &&
               std::ranges::equal(jpg.first(COMPATIBLE_JFIF_SIG.size()), COMPATIBLE_JFIF_SIG);
    }

    [[nodiscard]] std::optional<uint16_t> exifOrientation(std::span<const Byte> jpg) {
        constexpr std::size_t EXIF_SEARCH_LIMIT = 4096;
        constexpr auto APP1_SIG = std::to_array<Byte>({0xFF, 0xE1});

        auto app1_pos_opt = searchSig(jpg, APP1_SIG, EXIF_SEARCH_LIMIT);

        if (!app1_pos_opt) return std::nullopt;
        std::size_t pos = *app1_pos_opt;

        if (pos + 4 > jpg.size()) return std::nullopt;

        uint16_t segment_length = static_cast<uint16_t>((static_cast<uint16_t>(jpg[pos + 2]) << 8) | static_cast<uint16_t>(jpg[pos + 3]));
        std::size_t exif_end = pos + 2 + segment_length;

        if (segment_length < 2 || exif_end > jpg.size()) return std::nullopt;

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
        explicit operator bool() const { return handle != nullptr; }
    };

    void validateJpegHeader(std::span<const Byte> jpg) {
        TJHandle decompressor;
        decompressor.handle = tjInitDecompress();

        if (!decompressor) {
            throw std::runtime_error("tjInitDecompress() failed.");
        }

        int width = 0, height = 0, jpeg_subsamp = 0, jpeg_colorspace = 0;

        if (tjDecompressHeader3(decompressor.get(), jpg.data(), static_cast<unsigned long>(jpg.size()), &width, &height, &jpeg_subsamp, &jpeg_colorspace) != 0) {
            throw std::runtime_error(std::format("Image Error: {}", tjGetErrorStr2(decompressor.get())));
        }

        validateImageDimensions(width, height);
    }

    struct TJBuffer {
        unsigned char* data = nullptr;

        TJBuffer() = default;
        ~TJBuffer() { if (data) tjFree(data); }

        TJBuffer(const TJBuffer&) = delete;
        TJBuffer& operator=(const TJBuffer&) = delete;

        TJBuffer(TJBuffer&& other) noexcept : data(other.data) {
            other.data = nullptr;
        }

        TJBuffer& operator=(TJBuffer&& other) noexcept {
            if (this != &other) {
                if (data) tjFree(data);
                data = other.data;
                other.data = nullptr;
            }
            return *this;
        }
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

        validateImageDimensions(width, height);

        auto ori_opt = exifOrientation(jpg_vec);
        int xop = TJXOP_NONE;

        if (ori_opt) {
            xop = getTransformOp(*ori_opt);
        }

        tjtransform xform{};
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

        if (tjDecompressHeader3(decompressor.get(), image_file_vec.data(), static_cast<unsigned long>(image_file_vec.size()), &width, &height, &jpegSubsamp, &jpegColorspace) != 0) {
            throw std::runtime_error(std::format("tjDecompressHeader3: {}", tjGetErrorStr2(decompressor.get())));
        }

        validateImageDimensions(width, height);

        if (width < decrease_dims_val || height < decrease_dims_val) {
            throw std::runtime_error(std::format("Image is too small to decrease by {} pixels.", decrease_dims_val));
        }

        const int
            PIXEL_FORMAT    = TJPF_XBGR,
            BYTES_PER_PIXEL = tjPixelSize[PIXEL_FORMAT];

        vBytes decoded_image_vec(checkedPixelBufferSize(width, height, BYTES_PER_PIXEL));

        if (tjDecompress2(decompressor.get(), image_file_vec.data(), static_cast<unsigned long>(image_file_vec.size()), decoded_image_vec.data(), width, 0, height, PIXEL_FORMAT, 0) != 0) {
            throw std::runtime_error(std::format("tjDecompress2: {}", tjGetErrorStr2(decompressor.get())));
        }

        decompressor.reset();

        int
            newWidth  = width  - decrease_dims_val,
            newHeight = height - decrease_dims_val;

        if (newWidth < MIN_COVER_IMAGE_DIMENSION || newHeight < MIN_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error("Image Compatibility Error: Unable to remove close-comment block sequences without shrinking below the 400px minimum.");
        }

        std::print("\r{:44}\r", "");
        std::print("Quality: {}% | Width: {} | Height: {}", quality_val, newWidth, newHeight);
        std::fflush(stdout);

        vBytes resized_image_vec(checkedPixelBufferSize(newWidth, newHeight, BYTES_PER_PIXEL));

        if (!stbir_resize_uint8_srgb(decoded_image_vec.data(), width, height, 0, resized_image_vec.data(), newWidth, newHeight, 0, STBIR_4CHANNEL)) {
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
            subsamp = TJSAMP_420,
            flags   = TJFLAG_PROGRESSIVE;

        if (tjCompress2(compressor.get(), resized_image_vec.data(), newWidth, 0, newHeight, PIXEL_FORMAT, &jpegBuf.data, &jpegSize, subsamp, quality_val, flags) != 0) {
            throw std::runtime_error(std::format("tjCompress2: {}", tjGetErrorStr2(compressor.get())));
        }

        image_file_vec.assign(jpegBuf.data, jpegBuf.data + jpegSize);
    }
}

bool ensureImageCompatible(vBytes& image_file_vec) {
    constexpr std::size_t MARKER_INDEX = 0x0D;
    constexpr auto COMMENT_BLOCK_SIG = std::to_array<Byte>({ 0x23, 0x3E });

    if (image_file_vec.size() <= MARKER_INDEX) {
        throw std::runtime_error("Image File Error: Image too small to process.");
    }

    validateJpegHeader(image_file_vec);

    if (image_file_vec[MARKER_INDEX] == 0x19 &&
        hasCompatibleJfifHeader(image_file_vec) &&
        !searchSig(image_file_vec, COMMENT_BLOCK_SIG)) {
        return false;
    }

    int quality_val = 97, decrease_attempts = 300, decrease_dims_val = 0;

    std::println("\nChecking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n");
    std::println("Image quality & dimensions will be reduced in an attempt to remove them.\n");

    optimizeImage(image_file_vec);

    constexpr std::size_t DQT_SEARCH_LIMIT = 100;

    constexpr auto
        DQT1_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x43 }),
        DQT2_SIG = std::to_array<Byte>({ 0xFF, 0xDB, 0x00, 0x84 });

    auto
        dqt1 = searchSig(image_file_vec, DQT1_SIG, DQT_SEARCH_LIMIT),
        dqt2 = searchSig(image_file_vec, DQT2_SIG, DQT_SEARCH_LIMIT);

    if (!dqt1 && !dqt2) {
        throw std::runtime_error("Image File Error: No DQT segment found (corrupt or unsupported JPG).");
    }

    constexpr auto SIZE_MAX_VAL = std::numeric_limits<std::size_t>::max();
    std::size_t dqt_pos = std::min(dqt1.value_or(SIZE_MAX_VAL), dqt2.value_or(SIZE_MAX_VAL));

    constexpr auto JFIF_SIG = std::to_array<Byte>({ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 });

    // Replace everything before DQT with a clean JFIF header.
    vBytes cleaned(JFIF_SIG.begin(), JFIF_SIG.end());
    cleaned.insert(cleaned.end(),
        image_file_vec.begin() + static_cast<std::ptrdiff_t>(dqt_pos),
        image_file_vec.end());
    image_file_vec = std::move(cleaned);

    vBytes image_file_vec_copy = image_file_vec;

    auto index_opt = searchSig(image_file_vec, COMMENT_BLOCK_SIG);

    while (index_opt) {
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

    return true;
}
