#include "args.h"

#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {
    void printInfo() {
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
Max image size is 5MB.

https://github.com/CleasbyCode/jpws

    )");
    }

    [[nodiscard]] std::string displayProgramName(std::string_view program_name) {
        std::string prog = fs::path(program_name).filename().string();
        if (prog.empty()) {
            prog = "jpws";
        }
        return prog;
    }

    [[nodiscard]] std::runtime_error usageError(std::string_view program_name) {
        constexpr std::string_view PREFIX = "Usage: ";
        const std::string prog = displayProgramName(program_name);
        return std::runtime_error(std::format("{}{} [-alt] <cover_image> <pwsh_script>\n"
                                              "{}{} --info",
                                              PREFIX,
                                              prog,
                                              std::string(PREFIX.size(), ' '),
                                              prog));
    }

    [[nodiscard]] const char* argumentAt(int argc, char** argv, int index, const char* program_name) {
        if (argv == nullptr || index < 0 || index >= argc || argv[index] == nullptr) {
            throw usageError(program_name);
        }
        return argv[index];
    }

    [[nodiscard]] int parseOptionAndGetPathIndex(int argc, char** argv, const char* program_name, ProgramArgs& out) {
        if (argc == 4) {
            if (std::string_view(argumentAt(argc, argv, 1, program_name)) != "-alt") {
                throw usageError(program_name);
            }
            out.option = Option::Alt;
            return 2;
        }

        if (argc != 3) {
            throw usageError(program_name);
        }

        return 1;
    }

    void assignInputPaths(ProgramArgs& out, int path_arg, int argc, char** argv, const char* program_name) {
        out.image_file_path = fs::path(argumentAt(argc, argv, path_arg, program_name));
        out.pwsh_file_path = fs::path(argumentAt(argc, argv, path_arg + 1, program_name));

        if (out.image_file_path.empty() || out.pwsh_file_path.empty()) {
            throw usageError(program_name);
        }
    }
}

std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) {
    const char* program_name = (argc > 0 && argv != nullptr && argv[0] != nullptr) ? argv[0] : "jpws";

    if (argc < 2) {
        throw usageError(program_name);
    }

    if (std::string_view(argumentAt(argc, argv, 1, program_name)) == "--info") {
        if (argc != 2) {
            throw usageError(program_name);
        }
        printInfo();
        return std::nullopt;
    }

    ProgramArgs out{};
    const int path_arg = parseOptionAndGetPathIndex(argc, argv, program_name, out);
    assignInputPaths(out, path_arg, argc, argv, program_name);
    return out;
}
