#include "common.h"
#include "args.h"
#include "file_utils.h"
#include "jpeg_process.h"
#include "jpeg_warning_check.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <exception>
#include <fcntl.h>
#include <format>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {
    constexpr std::size_t JFIF_SIG_LENGTH = 20;
    constexpr std::size_t JFIF_COMMENT_BLOCK_INDEX = 0x0C;
    constexpr std::size_t SEGMENT_SIZE_FIELD_INDEX = 0x16;
    constexpr std::size_t PROFILE_SIZE_FIELD_INDEX = 0x26;
    constexpr std::size_t PWSH_INSERT_INDEX = 6;

    [[nodiscard]] std::size_t checkedAdd(std::size_t lhs, std::size_t rhs, std::string_view context) {
        if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
            throw std::runtime_error(std::format("{} size overflow.", context));
        }
        return lhs + rhs;
    }

    [[nodiscard]] std::size_t checkedSub(std::size_t lhs, std::size_t rhs, std::string_view context) {
        if (lhs < rhs) {
            throw std::runtime_error(std::format("{} size underflow.", context));
        }
        return lhs - rhs;
    }

    void writeBigEndian(vBytes& vec, std::size_t index, std::size_t value, std::size_t bytes) {
        if (bytes == 0 || bytes > sizeof(std::size_t)) {
            throw std::runtime_error("writeBigEndian: invalid byte count.");
        }
        if (index > vec.size() || bytes > vec.size() - index) {
            throw std::runtime_error("writeBigEndian: index out of bounds.");
        }
        if (bytes < sizeof(std::size_t) && value >= (std::size_t{1} << (bytes * 8))) {
            throw std::runtime_error("writeBigEndian: value exceeds range for requested byte width.");
        }

        constexpr std::size_t BITS_PER_BYTE = 8;
        for (std::size_t byte_index = 0; byte_index < bytes; ++byte_index) {
            const std::size_t shift = (bytes - byte_index - 1) * BITS_PER_BYTE;
            vec[index + byte_index] = static_cast<Byte>((value >> shift) & 0xFF);
        }
    }

    constexpr auto ICC_PROFILE_TEMPLATE = std::to_array<Byte>({
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
    });

    [[nodiscard]] std::string randomOutputSuffix(std::random_device& rd) {
        constexpr std::uint64_t OUTPUT_SUFFIX_MASK = (std::uint64_t{1} << 52) - 1;
        std::uniform_int_distribution<std::uint64_t> dist(0, OUTPUT_SUFFIX_MASK);
        return std::format("{:013x}", dist(rd));
    }

    void unlinkOutputFileBestEffort(const std::string& output_filename) noexcept {
        constexpr int MAX_UNLINK_RETRIES = 16;
        for (int attempt = 0; attempt < MAX_UNLINK_RETRIES; ++attempt) {
            if (::unlink(output_filename.c_str()) == 0 || errno != EINTR) {
                return;
            }
        }
    }

    [[noreturn]] void unlinkAndThrow(const std::string& output_filename, std::string_view message) {
        unlinkOutputFileBestEffort(output_filename);
        throw std::runtime_error(std::string(message));
    }

    [[noreturn]] void unlinkAndThrow(const std::string& output_filename, std::string_view prefix, int error_code) {
        unlinkOutputFileBestEffort(output_filename);
        throw std::runtime_error(std::format("{}: {}", prefix, std::system_category().message(error_code)));
    }

    void writeOutputBytes(const std::string& output_filename, int fd, std::span<const Byte> bytes) {
        while (!bytes.empty()) {
            const auto chunk_size = std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto bytes_written = ::write(fd, bytes.data(), chunk_size);

            if (bytes_written > 0) {
                bytes = bytes.subspan(static_cast<std::size_t>(bytes_written));
                continue;
            }
            if (bytes_written == 0) {
                unlinkAndThrow(output_filename, "Write Error: Failed to write complete output file.");
            }
            if (errno != EINTR) {
                unlinkAndThrow(output_filename, "Write Error: Failed to write output file", errno);
            }
        }
    }

    [[nodiscard]] bool startsWith(std::span<const Byte> bytes, std::span<const Byte> prefix) {
        return bytes.size() >= prefix.size() &&
            std::ranges::equal(bytes.first(prefix.size()), prefix);
    }

    void rejectUnsupportedTextEncoding(std::span<const Byte> bytes) {
        // UTF-32 BOMs share a prefix with UTF-16, so check 4-byte BOMs first.
        constexpr auto UTF32_LE_BOM = std::to_array<Byte>({ 0xFF, 0xFE, 0x00, 0x00 });
        constexpr auto UTF32_BE_BOM = std::to_array<Byte>({ 0x00, 0x00, 0xFE, 0xFF });
        constexpr auto UTF16_LE_BOM = std::to_array<Byte>({ 0xFF, 0xFE });
        constexpr auto UTF16_BE_BOM = std::to_array<Byte>({ 0xFE, 0xFF });

        const std::span<const Byte> view(bytes);
        if (startsWith(view, UTF32_LE_BOM) || startsWith(view, UTF32_BE_BOM)) {
            throw std::runtime_error(
                "Script File Error: PowerShell script is UTF-32 encoded. "
                "Re-save the script as UTF-8 (with or without BOM).");
        }
        if (startsWith(view, UTF16_LE_BOM) || startsWith(view, UTF16_BE_BOM)) {
            throw std::runtime_error(
                "Script File Error: PowerShell script is UTF-16 encoded. "
                "Re-save the script as UTF-8 (with or without BOM).");
        }
    }

    void stripUtf8Bom(vBytes& bytes) {
        constexpr auto BOM_SIG = std::to_array<Byte>({ 0xEF, 0xBB, 0xBF });

        if (bytes.size() >= BOM_SIG.size() &&
            std::ranges::equal(std::span<const Byte>(bytes).first(BOM_SIG.size()), BOM_SIG)) {
            bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(BOM_SIG.size()));
        }
    }

    struct ProfileSizes {
        std::size_t segment_size = 0;
        std::size_t profile_size = 0;
    };

    [[nodiscard]] ProfileSizes calculateProfileSizes(std::size_t profile_byte_count) {
        const std::size_t segment_size = checkedSub(
            checkedAdd(profile_byte_count, JFIF_SIG_LENGTH, "Profile segment"),
            SEGMENT_SIZE_FIELD_INDEX,
            "Profile segment");

        return ProfileSizes{
            .segment_size = segment_size,
            .profile_size = checkedSub(segment_size, 16, "ICC profile")
        };
    }

    void validateProfileSizes(const ProfileSizes& sizes) {
        constexpr std::size_t MAX_PROFILE_SEGMENT_SIZE = 10 * 1024;

        if (sizes.segment_size > MAX_PROFILE_SEGMENT_SIZE) {
            throw std::runtime_error("Segment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.");
        }

        if (sizes.segment_size > std::numeric_limits<uint16_t>::max()) {
            throw std::runtime_error("Segment Size Error: Segment size exceeds 16-bit field capacity.");
        }
    }

    [[nodiscard]] vBytes buildProfilePayload(std::span<const Byte> script_bytes) {
        constexpr std::size_t MIN_SCRIPT_PAYLOAD_SIZE = 10;
        if (script_bytes.size() < MIN_SCRIPT_PAYLOAD_SIZE) {
            throw std::runtime_error("Script File Error: PowerShell script is below minimum size.");
        }

        static_assert(ICC_PROFILE_TEMPLATE.size() >= PWSH_INSERT_INDEX);

        vBytes profile_vec(ICC_PROFILE_TEMPLATE.begin(), ICC_PROFILE_TEMPLATE.end());
        profile_vec.reserve(checkedAdd(profile_vec.size(), script_bytes.size(), "ICC profile"));
        profile_vec.insert(profile_vec.end() - PWSH_INSERT_INDEX, script_bytes.begin(), script_bytes.end());

        const ProfileSizes sizes = calculateProfileSizes(profile_vec.size());
        validateProfileSizes(sizes);

        constexpr std::size_t PROFILE_SEGMENT_SIZE_FIELD = SEGMENT_SIZE_FIELD_INDEX - JFIF_SIG_LENGTH;
        constexpr std::size_t PROFILE_PROFILE_SIZE_FIELD = PROFILE_SIZE_FIELD_INDEX - JFIF_SIG_LENGTH;
        writeBigEndian(profile_vec, PROFILE_SEGMENT_SIZE_FIELD, sizes.segment_size, 2);
        writeBigEndian(profile_vec, PROFILE_PROFILE_SIZE_FIELD, sizes.profile_size, 4);

        return profile_vec;
    }

    [[nodiscard]] constexpr auto makeDefaultTailBytes() {
        return std::to_array<Byte>({
            0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00
        });
    }

    [[nodiscard]] constexpr auto makeAltTailBytes() {
        return std::to_array<Byte>({
            0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e
        });
    }

    void patchImageTail(vBytes& image_file_vec, Option option) {
        constexpr auto DEFAULT_BYTES = makeDefaultTailBytes();
        constexpr auto ALT_BYTES = makeAltTailBytes();

        constexpr std::size_t TAIL_OFFSET = 2;

        const std::span<const Byte> tail_bytes = [&] {
            switch (option) {
                case Option::Alt:
                    return std::span<const Byte>(ALT_BYTES);
                case Option::None:
                    return std::span<const Byte>(DEFAULT_BYTES);
            }
            throw std::runtime_error("Internal Error: Unknown tail patch option.");
        }();

        if (image_file_vec.size() < JFIF_SIG_LENGTH ||
            image_file_vec.size() < tail_bytes.size() + TAIL_OFFSET) {
            throw std::runtime_error("Image File Error: Image too small after processing to apply tail patch.");
        }

        std::ranges::reverse_copy(tail_bytes, image_file_vec.rbegin() + TAIL_OFFSET);
    }

    void writeJfifCommentBlock(vBytes& image_file_vec) {
        constexpr auto JFIF_COMMENT_BLOCK = std::to_array<Byte>({ 0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23 });

        if (image_file_vec.size() < JFIF_COMMENT_BLOCK_INDEX + JFIF_COMMENT_BLOCK.size()) {
            throw std::runtime_error("Image File Error: Image too small to write JFIF comment block.");
        }

        std::ranges::copy(JFIF_COMMENT_BLOCK, image_file_vec.begin() + JFIF_COMMENT_BLOCK_INDEX);
    }

    [[nodiscard]] std::string writeOutputFile(std::initializer_list<std::span<const Byte>> parts) {
        constexpr mode_t OUTPUT_MODE = S_IRUSR | S_IWUSR;
        constexpr int OUTPUT_FLAGS = O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW;
        constexpr int MAX_OUTPUT_ATTEMPTS = 64;

        std::random_device rd;

        for (int attempt = 0; attempt < MAX_OUTPUT_ATTEMPTS; ++attempt) {
            std::string output_filename = std::format("jpws_{}.jpg", randomOutputSuffix(rd));
            UniqueFd output_fd(::open(output_filename.c_str(), OUTPUT_FLAGS, OUTPUT_MODE));

            if (output_fd.get() < 0) {
                if (errno == EEXIST) {
                    continue;
                }
                throw std::runtime_error(std::format("Write Error: Unable to create output file: {}", std::system_category().message(errno)));
            }

            for (const auto& part : parts) {
                writeOutputBytes(output_filename, output_fd.get(), part);
            }

            const int raw_fd = output_fd.release();
            if (::close(raw_fd) != 0) {
                unlinkAndThrow(output_filename, "Write Error: Failed to close output file", errno);
            }

            // Durably persist the output to disk before reporting success.
            // Failures here are non-fatal: the file is already written and the
            // caller is about to print "Saved …".
            {
                UniqueFd sync_fd(::open(output_filename.c_str(), O_RDONLY | O_CLOEXEC));
                if (sync_fd.get() >= 0) {
                    ::fsync(sync_fd.get());
                }
            }

            return output_filename;
        }

        throw std::runtime_error("Write Error: Unable to create a unique output filename after multiple attempts.");
    }

    [[nodiscard]] vBytes buildPolyglotOutputBytes(std::span<const Byte> image_file_vec, std::span<const Byte> profile_vec) {
        if (image_file_vec.size() < JFIF_SIG_LENGTH) {
            throw std::runtime_error("Image File Error: Image too small to write output.");
        }

        vBytes output_vec;
        output_vec.reserve(checkedAdd(image_file_vec.size(), profile_vec.size(), "Output image"));
        output_vec.insert(output_vec.end(), image_file_vec.begin(), image_file_vec.begin() + static_cast<std::ptrdiff_t>(JFIF_SIG_LENGTH));
        output_vec.insert(output_vec.end(), profile_vec.begin(), profile_vec.end());
        output_vec.insert(output_vec.end(), image_file_vec.begin() + static_cast<std::ptrdiff_t>(JFIF_SIG_LENGTH), image_file_vec.end());
        return output_vec;
    }

    [[nodiscard]] std::string writePolyglotOutput(std::span<const Byte> output_file_vec) {
        if (output_file_vec.empty()) {
            throw std::runtime_error("Image File Error: Output image is empty.");
        }

        return writeOutputFile({ output_file_vec });
    }

    [[nodiscard]] std::optional<std::size_t> tailCloseSequenceDistanceFromEnd(std::span<const Byte> bytes) {
        constexpr auto TAIL_CLOSE_SIG = std::to_array<Byte>({ 0x23, 0x3E });
        constexpr std::size_t TAIL_SEARCH_WINDOW = 256;

        const std::span<const Byte> tail = bytes.size() > TAIL_SEARCH_WINDOW ?
            bytes.last(TAIL_SEARCH_WINDOW) :
            bytes;

        auto match = std::ranges::search(tail, std::span<const Byte>(TAIL_CLOSE_SIG));
        if (match.empty()) {
            return std::nullopt;
        }

        return static_cast<std::size_t>(tail.end() - match.begin());
    }

    [[nodiscard]] vBytes buildTailStableOutput(
        std::span<const Byte> compatible_image_file_vec,
        std::span<const Byte> profile_vec,
        Option option,
        bool& reencoded_for_tail_stability) {

        constexpr int MAX_TAIL_STABILITY_RETRIES = 512;

        for (int attempt = 0; attempt <= MAX_TAIL_STABILITY_RETRIES; ++attempt) {
            vBytes candidate_image_file_vec;
            if (attempt == 0) {
                candidate_image_file_vec.assign(compatible_image_file_vec.begin(), compatible_image_file_vec.end());
            }
            else if (!makeTailRetryCandidate(compatible_image_file_vec, candidate_image_file_vec, attempt - 1)) {
                continue;
            }

            patchImageTail(candidate_image_file_vec, option);
            writeJfifCommentBlock(candidate_image_file_vec);

            vBytes output_vec = buildPolyglotOutputBytes(candidate_image_file_vec, profile_vec);
            const auto tail_close_distance = tailCloseSequenceDistanceFromEnd(output_vec);
            if (!tail_close_distance) {
                throw std::runtime_error("Tail Patch Error: Expected close-comment sequence was not present in output tail.");
            }
            const JpegWarningSummary warnings = inspectJpegWarnings(output_vec);
            if (!warnings.hasUnsafeTailWarning()) {
                reencoded_for_tail_stability = attempt > 0;
                return output_vec;
            }

            if (attempt == 0 && warnings.extraneous_data > 0) {
                std::println("\nTail validation reported extraneous bytes before EOI; re-encoding cover image for a safer tail.\n");
            }
        }

        throw std::runtime_error(
            "Image Compatibility Error: Unable to produce a tail-stable output image. "
            "Try another image or adjust JPEG quality/dimensions.");
    }
}

int main(int argc, char** argv) {
    try {
        auto args_opt = ProgramArgs::parse(argc, argv);
        if (!args_opt) {
            return 0;
        }

        const auto& args = *args_opt;

        vBytes image_file_vec = readFile(args.image_file_path, FileTypeCheck::cover_image);
        vBytes pwsh_file_vec = readFile(args.pwsh_file_path, FileTypeCheck::script_file);

        rejectUnsupportedTextEncoding(pwsh_file_vec);
        stripUtf8Bom(pwsh_file_vec);

        vBytes profile_vec = buildProfilePayload(pwsh_file_vec);

        const bool image_was_modified = ensureImageCompatible(image_file_vec);

        if (image_was_modified) {
            std::println("");
        }

        bool reencoded_for_tail_stability = false;
        vBytes output_file_vec = buildTailStableOutput(image_file_vec, profile_vec, args.option, reencoded_for_tail_stability);

        const std::size_t total_output_size = output_file_vec.size();
        const std::string output_filename = writePolyglotOutput(output_file_vec);

        std::println("\nSaved JPG-PowerShell polyglot image: {} ({} bytes).", output_filename, total_output_size);

        if (reencoded_for_tail_stability) {
            std::println("\nTail validation avoided an output with extraneous bytes before EOI.");
        }

        if (image_was_modified) {
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
    catch (const std::exception& e) {
        std::println(std::cerr, "\nUnexpected error: {}\n", e.what());
        return 1;
    }
}
