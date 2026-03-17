#pragma once

#include <filesystem>
#include <optional>

enum class Option : unsigned char { None, Alt };

struct ProgramArgs {
    Option option{Option::None};
    std::filesystem::path image_file_path;
    std::filesystem::path pwsh_file_path;

    static std::optional<ProgramArgs> parse(int argc, char** argv);
};

void displayInfo();
