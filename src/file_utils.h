#pragma once

#include "common.h"

#include <cstdint>
#include <filesystem>

enum class FileTypeCheck : std::uint8_t {
    cover_image = 1,
    script_file = 2
};

[[nodiscard]] vBytes readFile(const std::filesystem::path& path, FileTypeCheck file_type = FileTypeCheck::script_file);
