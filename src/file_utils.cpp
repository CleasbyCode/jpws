#include "file_utils.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <initializer_list>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace {
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
        std::string ext = p.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return std::ranges::any_of(exts, [&ext](std::string_view expected) {
            return ext == expected;
        });
    }
}

vBytes readFile(const fs::path& path, FileTypeCheck file_type) {
    if (!hasValidFilename(path)) {
        throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
    }

    // Use symlink_status (lstat) to detect symlinks without following them.
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec || !fs::exists(status)) {
        throw std::runtime_error(std::format("Error: File \"{}\" not found.", path.string()));
    }
    if (fs::is_symlink(status)) {
        throw std::runtime_error("Error: Symbolic links are not permitted.");
    }
    if (!fs::is_regular_file(status)) {
        throw std::runtime_error(std::format("Error: \"{}\" is not a regular file.", path.string()));
    }

    // Open with ios::ate to determine size from the stream position,
    // reducing the TOCTOU window vs. a separate fs::file_size() call.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::format("Failed to open file: {}", path.string()));
    }

    auto stream_pos = file.tellg();
    if (stream_pos < 0) {
        throw std::runtime_error("Failed to determine file size.");
    }
    auto file_size = static_cast<std::size_t>(stream_pos);

    if (!file_size) {
        throw std::runtime_error("Error: File is empty.");
    }

    if (file_type == FileTypeCheck::cover_image) {
        constexpr std::size_t
            MINIMUM_IMAGE_SIZE = 134,
            MAX_IMAGE_SIZE     = 5 * 1024 * 1024;

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

    if (file_type == FileTypeCheck::script_file) {
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

    file.seekg(0, std::ios::beg);
    vBytes vec(file_size);
    file.read(reinterpret_cast<char*>(vec.data()), static_cast<std::streamsize>(file_size));

    if (file.gcount() != static_cast<std::streamsize>(file_size)) {
        throw std::runtime_error("Failed to read full file: partial read");
    }

    // Validate JPEG SOI marker to reject non-JPEG files with .jpg extensions.
    if (file_type == FileTypeCheck::cover_image && (vec[0] != 0xFF || vec[1] != 0xD8)) {
        throw std::runtime_error("File Type Error: Not a valid JPEG file (missing SOI marker).");
    }

    return vec;
}
