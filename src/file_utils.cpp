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

    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        throw std::runtime_error(std::format("Error: File \"{}\" not found or not a regular file.", path.string()));
    }

    std::size_t file_size = fs::file_size(path);

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
