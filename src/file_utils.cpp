#include "file_utils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <format>
#include <fcntl.h>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>

namespace fs = std::filesystem;

namespace {
    [[noreturn]] void throwPathError(std::string_view action, const fs::path& path, int error_code) {
        throw std::runtime_error(std::format("{}: {} ({})", action, path.string(), std::system_category().message(error_code)));
    }

    struct OpenFileResult {
        UniqueFd fd;
        struct stat status{};
    };

    [[nodiscard]] bool hasValidFilename(const fs::path& p) {
        const std::string filename = p.filename().string();
        if (filename.empty()) return false;
        return std::ranges::all_of(filename, [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '-' || c == '_' || c == '@' || c == '%';
        });
    }

    [[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
        const std::string ext = p.extension().string();
        const auto ieq = [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == static_cast<unsigned char>(b);
        };
        return std::ranges::any_of(exts, [&](std::string_view expected) {
            return ext.size() == expected.size() &&
                   std::ranges::equal(ext, expected, ieq);
        });
    }

    [[nodiscard]] OpenFileResult openRegularFileNoFollow(const fs::path& path) {
        constexpr int OPEN_FLAGS = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;

        OpenFileResult result{ .fd = UniqueFd(::open(path.c_str(), OPEN_FLAGS)) };

        if (result.fd.get() < 0) {
            const int error_code = errno;

            if (error_code == ENOENT) {
                throw std::runtime_error(std::format("Error: File \"{}\" not found.", path.string()));
            }
            if (error_code == ELOOP) {
                throw std::runtime_error("Error: Symbolic links are not permitted.");
            }

            throwPathError("Failed to open file", path, error_code);
        }

        if (::fstat(result.fd.get(), &result.status) != 0) {
            throwPathError("Failed to stat file", path, errno);
        }

        if (!S_ISREG(result.status.st_mode)) {
            throw std::runtime_error(std::format("Error: \"{}\" is not a regular file.", path.string()));
        }

        return result;
    }

    [[nodiscard]] std::size_t fileSizeFromStat(const fs::path& path, const struct stat& status) {
        if (status.st_size <= 0) {
            throw std::runtime_error("Error: File is empty.");
        }

        const auto raw_size = static_cast<std::uintmax_t>(status.st_size);
        if (raw_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error(std::format("Error: File \"{}\" is too large to process on this platform.", path.string()));
        }

        return static_cast<std::size_t>(raw_size);
    }

    void validateFileType(const fs::path& path, std::size_t file_size, FileTypeCheck file_type) {
        switch (file_type) {
            case FileTypeCheck::cover_image:
                if (!hasFileExtension(path, {".jpg", ".jpeg", ".jfif"})) {
                    throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
                }
                if (file_size < 134) {
                    throw std::runtime_error("File Error: Invalid image file size.");
                }
                if (file_size > 5 * 1024 * 1024) {
                    throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
                }
                return;

            case FileTypeCheck::script_file:
                if (!hasFileExtension(path, {".ps1"})) {
                    throw std::runtime_error("File Type Error: Invalid script extension. Only expecting \".ps1\".");
                }
                if (file_size < 10) {
                    throw std::runtime_error("Script File Error: PowerShell script is below minimum size.");
                }
                if (file_size > 10 * 1024) {
                    throw std::runtime_error("Script File Error: PowerShell script exceeds maximum size limit.");
                }
                return;
        }
        throw std::runtime_error("Internal Error: Unknown file type check.");
    }

    void readExact(const fs::path& path, int fd, std::span<Byte> buffer) {
        std::size_t total_read = 0;

        while (total_read < buffer.size()) {
            const auto chunk_size = std::min<std::size_t>(buffer.size() - total_read, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            auto* buffer_ptr = buffer.data() + static_cast<std::ptrdiff_t>(total_read);
            const auto bytes_read = ::read(fd, buffer_ptr, chunk_size);

            if (bytes_read > 0) {
                total_read += static_cast<std::size_t>(bytes_read);
                continue;
            }
            if (bytes_read == 0) {
                throw std::runtime_error("Failed to read full file: unexpected EOF.");
            }
            if (errno != EINTR) {
                throwPathError("Failed to read file", path, errno);
            }
        }
    }

    [[nodiscard]] bool sameTimestamp(const timespec& lhs, const timespec& rhs) {
        return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
    }

    void verifyFileUnchanged(const fs::path& path, int fd, const struct stat& expected_status, std::size_t expected_size) {
        struct stat current_status{};

        if (::fstat(fd, &current_status) != 0) {
            throwPathError("Failed to stat file after read", path, errno);
        }

        if (!S_ISREG(current_status.st_mode)) {
            throw std::runtime_error(std::format("Error: \"{}\" is no longer a regular file.", path.string()));
        }

        if (current_status.st_dev != expected_status.st_dev ||
            current_status.st_ino != expected_status.st_ino ||
            current_status.st_mode != expected_status.st_mode ||
            current_status.st_size < 0 ||
            static_cast<std::uintmax_t>(current_status.st_size) != static_cast<std::uintmax_t>(expected_size) ||
            !sameTimestamp(current_status.st_mtim, expected_status.st_mtim) ||
            !sameTimestamp(current_status.st_ctim, expected_status.st_ctim)) {
            throw std::runtime_error(std::format("Error: File \"{}\" changed while it was being read.", path.string()));
        }
    }
}

vBytes readFile(const fs::path& path, FileTypeCheck file_type) {
    if (!hasValidFilename(path)) {
        throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
    }

    auto open_file = openRegularFileNoFollow(path);
    const std::size_t file_size = fileSizeFromStat(path, open_file.status);
    validateFileType(path, file_size, file_type);

    vBytes vec(file_size);
    readExact(path, open_file.fd.get(), vec);
    verifyFileUnchanged(path, open_file.fd.get(), open_file.status, file_size);

    // Validate JPEG SOI marker to reject non-JPEG files with .jpg extensions.
    if (file_type == FileTypeCheck::cover_image && (vec[0] != 0xFF || vec[1] != 0xD8)) {
        throw std::runtime_error("File Type Error: Not a valid JPEG file (missing SOI marker).");
    }

    return vec;
}
