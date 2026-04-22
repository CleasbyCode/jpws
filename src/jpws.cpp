#include "common.h"
#include "args.h"
#include "file_utils.h"
#include "jpeg_process.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <limits>
#include <print>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
    void writeBigEndian(vBytes& vec, std::size_t index, std::size_t value, std::size_t bytes) {
        if (bytes == 0 || bytes > sizeof(std::size_t)) {
            throw std::runtime_error("writeBigEndian: invalid byte count.");
        }
        if (index + bytes > vec.size()) {
            throw std::runtime_error("writeBigEndian: index out of bounds.");
        }
        if (bytes < sizeof(std::size_t) && value >= (std::size_t{1} << (bytes * 8))) {
            throw std::runtime_error("writeBigEndian: value exceeds range for requested byte width.");
        }
        for (std::size_t i = bytes; i-- > 0;) {
            vec[index++] = static_cast<Byte>((value >> (i * 8)) & 0xFF);
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
        return std::format("{:013x}",
                           (((static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd())) &
                            OUTPUT_SUFFIX_MASK));
    }

    [[noreturn]] void unlinkAndThrow(const std::string& output_filename, std::string_view message) {
        ::unlink(output_filename.c_str());
        throw std::runtime_error(std::string(message));
    }

    [[noreturn]] void unlinkAndThrow(const std::string& output_filename, std::string_view prefix, int error_code) {
        ::unlink(output_filename.c_str());
        throw std::runtime_error(std::format("{}: {}", prefix, std::system_category().message(error_code)));
    }

    [[nodiscard]] std::string writeOutputFile(const vBytes& bytes) {
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

            std::size_t total_written = 0;

            while (total_written < bytes.size()) {
                const auto chunk_size = std::min<std::size_t>(bytes.size() - total_written, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
                const auto* write_ptr = bytes.data() + static_cast<std::ptrdiff_t>(total_written);
                const auto bytes_written = ::write(output_fd.get(), write_ptr, chunk_size);

                if (bytes_written > 0) {
                    total_written += static_cast<std::size_t>(bytes_written);
                    continue;
                }
                if (bytes_written == 0) {
                    unlinkAndThrow(output_filename, "Write Error: Failed to write complete output file.");
                }
                if (errno != EINTR) {
                    unlinkAndThrow(output_filename, "Write Error: Failed to write output file", errno);
                }
            }

            const int raw_fd = output_fd.release();
            if (::close(raw_fd) != 0) {
                unlinkAndThrow(output_filename, "Write Error: Failed to close output file", errno);
            }

            return output_filename;
        }

        throw std::runtime_error("Write Error: Unable to create a unique output filename after multiple attempts.");
    }
}

int main(int argc, char** argv) {
    try {
        auto args_opt = ProgramArgs::parse(argc, argv);
        if (!args_opt) return 0;

        const auto& args = *args_opt;

        vBytes image_file_vec = readFile(args.image_file_path, FileTypeCheck::cover_image);

        constexpr std::size_t JFIF_SIG_LENGTH = 20;

        bool image_was_modified = ensureImageCompatible(image_file_vec);

        if (image_was_modified) std::println("");

        constexpr auto
            DEFAULT_BYTES = std::to_array<Byte>({ 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x23, 0x3E, 0x0D, 0x23, 0x9e }),
            ALT_BYTES     = std::to_array<Byte>({ 0x9e, 0x23, 0x3e, 0x0d, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00 });

        constexpr std::size_t TAIL_PATCH_SIZE = DEFAULT_BYTES.size();
        constexpr std::size_t TAIL_OFFSET = 2;
        static_assert(DEFAULT_BYTES.size() == ALT_BYTES.size());
        // Downstream operations rely on a full 20-byte JFIF header being present.
        if (image_file_vec.size() < JFIF_SIG_LENGTH ||
            image_file_vec.size() < TAIL_PATCH_SIZE + TAIL_OFFSET) {
            throw std::runtime_error("Image File Error: Image too small after processing to apply tail patch.");
        }
        const std::span<const Byte> tail_bytes = args.option == Option::Alt ? std::span<const Byte>(ALT_BYTES) : std::span<const Byte>(DEFAULT_BYTES);
        std::ranges::copy(tail_bytes | std::views::reverse, image_file_vec.rbegin() + TAIL_OFFSET);

        vBytes pwsh_file_vec = readFile(args.pwsh_file_path, FileTypeCheck::script_file);

        constexpr auto BOM_SIG = std::to_array<Byte>({ 0xEF, 0xBB, 0xBF });

        if (std::ranges::equal(pwsh_file_vec | std::views::take(BOM_SIG.size()), BOM_SIG)) {
            pwsh_file_vec.erase(pwsh_file_vec.begin(), pwsh_file_vec.begin() + 3);
        }

        vBytes profile_vec(ICC_PROFILE_TEMPLATE.begin(), ICC_PROFILE_TEMPLATE.end());

        constexpr std::size_t PWSH_INSERT_INDEX = 6;
        profile_vec.insert(profile_vec.end() - PWSH_INSERT_INDEX, pwsh_file_vec.begin(), pwsh_file_vec.end());

        constexpr std::size_t
            JFIF_COMMENT_BLOCK_INDEX = 0x0C,
            SEGMENT_SIZE_FIELD_INDEX = 0x16,
            PROFILE_SIZE_FIELD_INDEX = 0x26;

        const std::size_t
            SEGMENT_SIZE = (profile_vec.size() + JFIF_SIG_LENGTH) - SEGMENT_SIZE_FIELD_INDEX,
            PROFILE_SIZE = SEGMENT_SIZE - 16;

        constexpr std::size_t MAX_POWERSHELL_FILE_SIZE = 10 * 1024;

        if (SEGMENT_SIZE > MAX_POWERSHELL_FILE_SIZE) {
            throw std::runtime_error("Segment Size Error: The profile segment (FFE2) exceeds the maximum size limit of 10KB.");
        }

        if (SEGMENT_SIZE > std::numeric_limits<uint16_t>::max()) {
            throw std::runtime_error("Segment Size Error: Segment size exceeds 16-bit field capacity.");
        }

        image_file_vec.insert(image_file_vec.begin() + JFIF_SIG_LENGTH, profile_vec.begin(), profile_vec.end());

        writeBigEndian(image_file_vec, SEGMENT_SIZE_FIELD_INDEX, SEGMENT_SIZE, 2);
        writeBigEndian(image_file_vec, PROFILE_SIZE_FIELD_INDEX, PROFILE_SIZE, 4);

        constexpr auto JFIF_COMMENT_BLOCK = std::to_array<Byte>({ 0x58, 0x54, 0x57, 0x0A, 0x3C, 0x23 });

        if (image_file_vec.size() < JFIF_COMMENT_BLOCK_INDEX + JFIF_COMMENT_BLOCK.size()) {
            throw std::runtime_error("Image File Error: Image too small to write JFIF comment block.");
        }
        std::ranges::copy(JFIF_COMMENT_BLOCK, image_file_vec.begin() + JFIF_COMMENT_BLOCK_INDEX);

        const std::string OUTPUT_FILENAME = writeOutputFile(image_file_vec);

        std::println("\nSaved JPG-PowerShell polyglot image: {} ({} bytes).", OUTPUT_FILENAME, image_file_vec.size());

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
}
