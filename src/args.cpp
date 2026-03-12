#include "args.h"

#include <format>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

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

std::optional<ProgramArgs> ProgramArgs::parse(int argc, char** argv) {
    auto arg = [&](int i) -> std::string_view {
        return (i >= 0 && i < argc) ? std::string_view(argv[i]) : std::string_view{};
    };

    constexpr std::string_view PREFIX = "Usage: ";
    const std::string
        PROG = fs::path(argv[0]).filename().string(),
        INDENT(PREFIX.size(), ' '),
        USAGE = std::format("{}{} [-alt] <cover_image> <pwsh_script>\n"
                            "{}{} --info", PREFIX, PROG, INDENT, PROG);

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
