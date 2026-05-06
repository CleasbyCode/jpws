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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <span>
#include <stdexcept>
#include <utility>

namespace {
    constexpr int MIN_COVER_IMAGE_DIMENSION = 400;
    constexpr int MAX_COVER_IMAGE_DIMENSION = 8'192;
    constexpr std::size_t MAX_COVER_IMAGE_PIXELS = 25'000'000;

    struct ImageSize {
        int width = 0;
        int height = 0;
    };

    struct JpegSegment {
        Byte marker = 0;
        std::size_t marker_offset = 0;
        std::size_t payload_offset = 0;
        std::size_t payload_size = 0;
    };

    constexpr Byte JPEG_MARKER_SOS = 0xDA;
    constexpr Byte JPEG_MARKER_DQT = 0xDB;
    constexpr Byte JPEG_MARKER_APP1 = 0xE1;

    [[nodiscard]] std::span<const Byte> searchWindow(std::span<const Byte> v, std::size_t limit) {
        return (limit == 0 || limit > v.size()) ? v : v.first(limit);
    }

    [[nodiscard]] std::optional<std::size_t> findByte(std::span<const Byte> v, Byte byte) {
        if (v.empty()) {
            return std::nullopt;
        }

        const void* hit = std::memchr(v.data(), byte, v.size());
        if (hit == nullptr) {
            return std::nullopt;
        }

        return static_cast<std::size_t>(static_cast<const Byte*>(hit) - v.data());
    }

    [[nodiscard]] std::optional<std::size_t> findTwoByteSig(std::span<const Byte> v, Byte first, Byte second) {
        if (v.size() < 2) {
            return std::nullopt;
        }

        const Byte* const begin = v.data();
        const Byte* cursor = begin;
        const Byte* const last_candidate = begin + static_cast<std::ptrdiff_t>(v.size() - 1);

        while (cursor < last_candidate) {
            const auto remaining = static_cast<std::size_t>(last_candidate - cursor);
            const void* hit = std::memchr(cursor, first, remaining);
            if (hit == nullptr) {
                return std::nullopt;
            }

            const auto* pos = static_cast<const Byte*>(hit);
            if (pos[1] == second) {
                return static_cast<std::size_t>(pos - begin);
            }

            cursor = pos + 1;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> searchSig(std::span<const Byte> v, std::span<const Byte> sig, std::size_t limit = 0) {
        const std::span<const Byte> haystack = searchWindow(v, limit);

        if (sig.empty()) {
            return std::size_t{0};
        }
        if (sig.size() == 1) {
            return findByte(haystack, sig[0]);
        }
        if (sig.size() == 2) {
            return findTwoByteSig(haystack, sig[0], sig[1]);
        }

        auto it = std::ranges::search(haystack, sig);
        return it.empty() ? std::nullopt : std::optional(static_cast<std::size_t>(it.begin() - haystack.begin()));
    }

    [[nodiscard]] std::optional<uint16_t> readBigEndian16(std::span<const Byte> bytes, std::size_t offset) {
        if (offset > bytes.size() || bytes.size() - offset < 2) {
            return std::nullopt;
        }

        return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                     static_cast<uint16_t>(bytes[offset + 1]));
    }

    [[nodiscard]] bool markerHasNoPayload(Byte marker) {
        return marker == 0x01 || marker == 0xD8 || marker == 0xD9 ||
               (marker >= 0xD0 && marker <= 0xD7);
    }

    template <typename Predicate>
    [[nodiscard]] std::optional<JpegSegment> findJpegHeaderSegment(std::span<const Byte> jpg, Predicate predicate) {
        if (jpg.size() < 2 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
            return std::nullopt;
        }

        std::size_t pos = 2;

        while (pos < jpg.size()) {
            if (jpg[pos] != 0xFF) {
                return std::nullopt;
            }

            const std::size_t marker_offset = pos;

            while (pos < jpg.size() && jpg[pos] == 0xFF) {
                ++pos;
            }

            if (pos >= jpg.size()) {
                return std::nullopt;
            }

            const Byte marker = jpg[pos++];

            if (marker == 0x00) {
                return std::nullopt;
            }

            if (markerHasNoPayload(marker)) {
                continue;
            }

            const auto segment_length_opt = readBigEndian16(jpg, pos);
            if (!segment_length_opt) {
                return std::nullopt;
            }

            const std::size_t segment_length = *segment_length_opt;
            if (segment_length < 2 || pos > jpg.size() - segment_length) {
                return std::nullopt;
            }

            const std::size_t payload_offset = pos + 2;
            const JpegSegment segment{
                .marker = marker,
                .marker_offset = marker_offset,
                .payload_offset = payload_offset,
                .payload_size = segment_length - 2
            };

            if (predicate(segment)) {
                return segment;
            }

            if (marker == JPEG_MARKER_SOS) {
                return std::nullopt;
            }

            pos += segment_length;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::size_t checkedPixelCount(int width, int height) {
        if (width <= 0 || height <= 0) {
            throw std::runtime_error("Image Error: Invalid image dimensions.");
        }

        const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (pixel_count > MAX_COVER_IMAGE_PIXELS) {
            throw std::runtime_error("Image Error: Pixel count exceeds the supported maximum of 25 megapixels.");
        }

        return pixel_count;
    }

    void validateImageDimensions(int width, int height) {
        if (width < MIN_COVER_IMAGE_DIMENSION || height < MIN_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error("Image Error: Dimensions are too small.\nFor platform compatibility, cover image must be at least 400px for both width and height.");
        }

        if (width > MAX_COVER_IMAGE_DIMENSION || height > MAX_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error(std::format("Image Error: Dimensions exceed the supported maximum of {}px.", MAX_COVER_IMAGE_DIMENSION));
        }

        (void)checkedPixelCount(width, height);
    }

    [[nodiscard]] std::size_t checkedPixelBufferSize(int width, int height, int bytes_per_pixel) {
        if (bytes_per_pixel <= 0) {
            throw std::runtime_error("Image Error: Invalid pixel format.");
        }

        const auto pixel_count = checkedPixelCount(width, height);
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

    struct TiffReader {
        std::span<const Byte> bytes;
        bool little_endian = false;

        [[nodiscard]] std::optional<uint16_t> read16(std::size_t offset) const {
            if (offset > bytes.size() || bytes.size() - offset < 2) {
                return std::nullopt;
            }

            if (little_endian) {
                return static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset]) |
                                             (static_cast<uint16_t>(bytes[offset + 1]) << 8));
            }

            return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                         static_cast<uint16_t>(bytes[offset + 1]));
        }

        [[nodiscard]] std::optional<uint32_t> read32(std::size_t offset) const {
            if (offset > bytes.size() || bytes.size() - offset < 4) {
                return std::nullopt;
            }

            if (little_endian) {
                return static_cast<uint32_t>(bytes[offset]) |
                       (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                       (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                       (static_cast<uint32_t>(bytes[offset + 3]) << 24);
            }

            return (static_cast<uint32_t>(bytes[offset]) << 24) |
                   (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
                   (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
                   static_cast<uint32_t>(bytes[offset + 3]);
        }
    };

    [[nodiscard]] std::optional<bool> tiffIsLittleEndian(std::span<const Byte> tiff_data) {
        if (tiff_data.size() < 2) {
            return std::nullopt;
        }
        if (tiff_data[0] == 'I' && tiff_data[1] == 'I') {
            return true;
        }
        if (tiff_data[0] == 'M' && tiff_data[1] == 'M') {
            return false;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<uint16_t> exifOrientation(std::span<const Byte> jpg) {
        constexpr std::size_t EXIF_HEADER_SIZE = 6;
        constexpr auto EXIF_SIG = std::to_array<Byte>({'E', 'x', 'i', 'f', '\0', '\0'});

        const auto exif_segment = findJpegHeaderSegment(jpg, [&](const JpegSegment& segment) {
            return segment.marker == JPEG_MARKER_APP1 &&
                   segment.payload_size >= EXIF_HEADER_SIZE &&
                   std::ranges::equal(jpg.subspan(segment.payload_offset, EXIF_HEADER_SIZE), EXIF_SIG);
        });

        if (!exif_segment) {
            return std::nullopt;
        }

        std::span<const Byte> payload = jpg.subspan(exif_segment->payload_offset, exif_segment->payload_size);
        std::span<const Byte> tiff_data = payload.subspan(EXIF_HEADER_SIZE);

        if (tiff_data.size() < 8) return std::nullopt;

        const auto little_endian = tiffIsLittleEndian(tiff_data);
        if (!little_endian) return std::nullopt;

        const TiffReader tiff{ .bytes = tiff_data, .little_endian = *little_endian };

        if (tiff.read16(2) != 0x002A) return std::nullopt;

        const auto ifd_offset = tiff.read32(4);
        if (!ifd_offset) return std::nullopt;

        // Need at least 2 bytes at ifd_offset for the entry count.
        if (*ifd_offset < 8 ||
            static_cast<std::size_t>(*ifd_offset) > tiff_data.size() ||
            tiff_data.size() - static_cast<std::size_t>(*ifd_offset) < 2) {
            return std::nullopt;
        }

        const auto entry_count = tiff.read16(*ifd_offset);
        if (!entry_count) return std::nullopt;

        const std::size_t entries_offset = static_cast<std::size_t>(*ifd_offset) + 2;

        constexpr uint16_t TAG_ORIENTATION = 0x0112;
        constexpr uint16_t TIFF_TYPE_SHORT = 3;
        constexpr std::size_t ENTRY_SIZE = 12;

        // Bound the loop to the number of entries that actually fit — guards
        // against a malicious entry_count larger than the payload allows.
        const std::size_t max_entries =
            (tiff_data.size() - entries_offset) / ENTRY_SIZE;
        const std::size_t bounded_count =
            std::min<std::size_t>(*entry_count, max_entries);

        for (std::size_t i = 0, current_entry = entries_offset; i < bounded_count; ++i, current_entry += ENTRY_SIZE) {
            const auto tag_id = tiff.read16(current_entry);
            if (tag_id == TAG_ORIENTATION) {
                const auto type = tiff.read16(current_entry + 2);
                const auto count = tiff.read32(current_entry + 4);
                const auto value = tiff.read16(current_entry + 8);

                if (!type || !count || !value ||
                    *type != TIFF_TYPE_SHORT || *count != 1) {
                    return std::nullopt;
                }
                return value;
            }
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
        explicit TJHandle(tjhandle raw_handle = nullptr) : handle(raw_handle) {}
        ~TJHandle() { if (handle) tjDestroy(handle); }
        TJHandle(const TJHandle&) = delete;
        TJHandle& operator=(const TJHandle&) = delete;
        TJHandle(TJHandle&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
        TJHandle& operator=(TJHandle&& other) noexcept {
            if (this != &other) {
                if (handle) tjDestroy(handle);
                handle = std::exchange(other.handle, nullptr);
            }
            return *this;
        }
        [[nodiscard]] tjhandle get() const { return handle; }
        [[nodiscard]] explicit operator bool() const { return handle != nullptr; }
    };

    [[nodiscard]] TJHandle makeHandle(tjhandle raw_handle, std::string_view init_name) {
        if (!raw_handle) {
            throw std::runtime_error(std::format("{} failed.", init_name));
        }
        return TJHandle(raw_handle);
    }

    [[nodiscard]] unsigned long toTurboJpegSize(std::size_t size) {
        if (size == 0 || size > static_cast<std::size_t>(std::numeric_limits<unsigned long>::max())) {
            throw std::runtime_error("Image Error: JPEG buffer size is unsupported by TurboJPEG.");
        }
        return static_cast<unsigned long>(size);
    }

    [[nodiscard]] ImageSize readJpegSize(tjhandle handle, std::span<const Byte> jpg, std::string_view error_prefix) {
        ImageSize size{};
        int jpeg_subsamp = 0, jpeg_colorspace = 0;

        if (tjDecompressHeader3(handle, jpg.data(), toTurboJpegSize(jpg.size()), &size.width, &size.height, &jpeg_subsamp, &jpeg_colorspace) != 0) {
            throw std::runtime_error(std::format("{}: {}", error_prefix, tjGetErrorStr2(handle)));
        }

        validateImageDimensions(size.width, size.height);
        return size;
    }

    void validateJpegHeader(std::span<const Byte> jpg) {
        auto decompressor = makeHandle(tjInitDecompress(), "tjInitDecompress()");
        (void)readJpegSize(decompressor.get(), jpg, "Image Error");
    }

    struct TJBuffer {
        unsigned char* data = nullptr;
        TJBuffer() = default;
        ~TJBuffer() { if (data) tjFree(data); }
        TJBuffer(const TJBuffer&) = delete;
        TJBuffer& operator=(const TJBuffer&) = delete;
    };

    void assignFromTJBuffer(vBytes& out, const TJBuffer& buffer, unsigned long byte_count) {
        if (!buffer.data || byte_count == 0) {
            throw std::runtime_error("Image Error: TurboJPEG produced an empty output buffer.");
        }

        if (byte_count > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max()) ||
            byte_count > static_cast<unsigned long>(std::numeric_limits<std::ptrdiff_t>::max())) {
            throw std::runtime_error("Image Error: TurboJPEG output is too large to store on this platform.");
        }

        const auto size = static_cast<std::size_t>(byte_count);
        out.assign(buffer.data, buffer.data + size);
    }

    struct DecodedImage {
        ImageSize size{};
        int pixel_format = TJPF_RGB;
        stbir_pixel_layout resize_pixel_layout = STBIR_RGB;
        int bytes_per_pixel = 0;
        vBytes pixels;
    };

    [[nodiscard]] DecodedImage decodeJpeg(std::span<const Byte> jpg) {
        auto decompressor = makeHandle(tjInitDecompress(), "tjInitDecompress()");
        const auto image_size = readJpegSize(decompressor.get(), jpg, "tjDecompressHeader3");

        constexpr int PIXEL_FORMAT = TJPF_RGB;
        constexpr stbir_pixel_layout RESIZE_PIXEL_LAYOUT = STBIR_RGB;
        const int bytes_per_pixel = tjPixelSize[PIXEL_FORMAT];

        vBytes decoded_image_vec(checkedPixelBufferSize(image_size.width, image_size.height, bytes_per_pixel));

        if (tjDecompress2(decompressor.get(), jpg.data(), toTurboJpegSize(jpg.size()), decoded_image_vec.data(), image_size.width, 0, image_size.height, PIXEL_FORMAT, 0) != 0) {
            throw std::runtime_error(std::format("tjDecompress2: {}", tjGetErrorStr2(decompressor.get())));
        }

        return DecodedImage{
            .size = image_size,
            .pixel_format = PIXEL_FORMAT,
            .resize_pixel_layout = RESIZE_PIXEL_LAYOUT,
            .bytes_per_pixel = bytes_per_pixel,
            .pixels = std::move(decoded_image_vec)
        };
    }

    void optimizeImage(vBytes& jpg_vec) {
        if (jpg_vec.empty()) {
            throw std::runtime_error("JPG image is empty!");
        }

        auto transformer = makeHandle(tjInitTransform(), "tjInitTransform()");
        (void)readJpegSize(transformer.get(), jpg_vec, "Image Error");

        tjtransform xform{};
        xform.op = getTransformOp(exifOrientation(jpg_vec).value_or(1));
        xform.options = TJXOPT_COPYNONE | TJXOPT_TRIM | TJXOPT_PROGRESSIVE;

        TJBuffer dstBuffer;
        unsigned long dstSize = 0;

        if (tjTransform(transformer.get(), jpg_vec.data(), toTurboJpegSize(jpg_vec.size()), 1, &dstBuffer.data, &dstSize, &xform, 0) != 0) {
            throw std::runtime_error(std::format("Image Error: {}", tjGetErrorStr2(transformer.get())));
        }

        assignFromTJBuffer(jpg_vec, dstBuffer, dstSize);
    }

    void resizeImage(vBytes& image_file_vec, const DecodedImage& source, vBytes& resize_scratch, tjhandle compressor, int quality_val, int decrease_dims_val) {
        if (quality_val < 1 || quality_val > 100) {
            throw std::runtime_error("Image Error: JPEG quality value is outside the supported range.");
        }

        if (source.size.width < decrease_dims_val || source.size.height < decrease_dims_val) {
            throw std::runtime_error(std::format("Image is too small to decrease by {} pixels.", decrease_dims_val));
        }

        int
            newWidth  = source.size.width  - decrease_dims_val,
            newHeight = source.size.height - decrease_dims_val;

        if (newWidth < MIN_COVER_IMAGE_DIMENSION || newHeight < MIN_COVER_IMAGE_DIMENSION) {
            throw std::runtime_error("Image Compatibility Error: Unable to remove close-comment block sequences without shrinking below the 400px minimum.");
        }

        std::print("\r{:44}\r", "");
        std::print("Quality: {}% | Width: {} | Height: {}", quality_val, newWidth, newHeight);
        std::fflush(stdout);

        resize_scratch.resize(checkedPixelBufferSize(newWidth, newHeight, source.bytes_per_pixel));

        if (!stbir_resize_uint8_srgb(source.pixels.data(), source.size.width, source.size.height, 0, resize_scratch.data(), newWidth, newHeight, 0, source.resize_pixel_layout)) {
            throw std::runtime_error("stbir_resize_uint8_srgb failed.");
        }

        TJBuffer jpegBuf;
        unsigned long jpegSize = 0;
        int
            subsamp = TJSAMP_420,
            flags   = TJFLAG_PROGRESSIVE;

        if (tjCompress2(compressor, resize_scratch.data(), newWidth, 0, newHeight, source.pixel_format, &jpegBuf.data, &jpegSize, subsamp, quality_val, flags) != 0) {
            throw std::runtime_error(std::format("tjCompress2: {}", tjGetErrorStr2(compressor)));
        }

        assignFromTJBuffer(image_file_vec, jpegBuf, jpegSize);
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

    std::println("\nChecking cover image for comment-block close sequences \"#>\" (0x23, 0x3E).\n");
    std::println("Image quality & dimensions will be reduced in an attempt to remove them.\n");

    optimizeImage(image_file_vec);

    const auto dqt = findJpegHeaderSegment(image_file_vec, [](const JpegSegment& segment) {
        return segment.marker == JPEG_MARKER_DQT;
    });

    if (!dqt) {
        throw std::runtime_error("Image File Error: No DQT segment found (corrupt or unsupported JPG).");
    }

    const std::size_t dqt_pos = dqt->marker_offset;

    constexpr auto JFIF_SIG = std::to_array<Byte>({ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 });

    // Replace everything before DQT with a clean JFIF header.
    vBytes cleaned;
    cleaned.reserve(JFIF_SIG.size() + image_file_vec.size() - dqt_pos);
    cleaned.insert(cleaned.end(), JFIF_SIG.begin(), JFIF_SIG.end());
    cleaned.insert(cleaned.end(),
        image_file_vec.begin() + static_cast<std::ptrdiff_t>(dqt_pos),
        image_file_vec.end());
    image_file_vec = std::move(cleaned);
    validateJpegHeader(image_file_vec);

    // Fast path: header cleanup alone removed all "#>" sequences — skip the
    // expensive buffer copy and the resize loop entirely.
    if (!searchSig(image_file_vec, COMMENT_BLOCK_SIG)) {
        return true;
    }

    const DecodedImage pristine = decodeJpeg(image_file_vec);
    vBytes resize_scratch;
    auto compressor = makeHandle(tjInitCompress(), "tjInitCompress()");

    constexpr int MAX_RESIZE_ATTEMPTS = 300;
    constexpr int START_QUALITY = 97;

    for (int attempt = 1; attempt <= MAX_RESIZE_ATTEMPTS; ++attempt) {
        const int decrease_dims_val = attempt;
        const int quality_val = START_QUALITY - ((attempt / 15) * 2);

        resizeImage(image_file_vec, pristine, resize_scratch, compressor.get(), quality_val, decrease_dims_val);

        if (!searchSig(image_file_vec, COMMENT_BLOCK_SIG)) {
            return true;
        }
    }

    std::println(std::cerr, "\n\nImage Compatibility Error:\n\nProcedure failed to remove close-comment block sequences from cover image.");
    throw std::runtime_error("Try another image or use an editor such as GIMP to manually reduce (scale) image dimensions.");
}
