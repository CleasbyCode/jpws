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
    constexpr std::size_t MIN_COVER_IMAGE_FILE_SIZE = 134;
    constexpr std::size_t MAX_COVER_IMAGE_FILE_SIZE = 5 * 1024 * 1024;
    constexpr std::size_t MIN_SCRIPT_FILE_SIZE = 10;

    [[noreturn]] void throwPathError(std::string_view action, const fs::path& path, int error_code) {
        throw std::runtime_error(std::format("{}: {} ({})", action, path.string(), std::system_category().message(error_code)));
    }

    struct OpenFileResult {
        UniqueFd fd;
        struct stat status{};
    };

    [[nodiscard]] bool hasValidFilename(const fs::path& p) {
        const std::string filename = p.filename().string();
        if (filename.empty() || filename == "." || filename == "..") {
            return false;
        }
        return std::ranges::none_of(filename, [](unsigned char c) {
            return c < 0x20 || c == 0x7F;
        });
    }

    [[nodiscard]] bool hasFileExtension(const fs::path& p, std::initializer_list<std::string_view> exts) {
        const std::string ext = p.extension().string();
        const auto ieq = [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        };
        return std::ranges::any_of(exts, [&](std::string_view expected) {
            return ext.size() == expected.size() &&
                   std::ranges::equal(ext, expected, ieq);
        });
    }

    void validateCoverImageFile(const fs::path& path, std::size_t file_size) {
        if (!hasFileExtension(path, {".jpg", ".jpeg", ".jfif"})) {
            throw std::runtime_error("File Type Error: Invalid image extension. Only expecting \".jpg\", \".jpeg\", or \".jfif\".");
        }
        if (file_size < MIN_COVER_IMAGE_FILE_SIZE) {
            throw std::runtime_error("File Error: Invalid image file size.");
        }
        if (file_size > MAX_COVER_IMAGE_FILE_SIZE) {
            throw std::runtime_error("Image File Error: Cover image file exceeds maximum size limit.");
        }
    }

    void validateScriptFile(const fs::path& path, std::size_t file_size) {
        if (!hasFileExtension(path, {".ps1"})) {
            throw std::runtime_error("File Type Error: Invalid script extension. Only expecting \".ps1\".");
        }
        if (file_size < MIN_SCRIPT_FILE_SIZE) {
            throw std::runtime_error("Script File Error: PowerShell script is below minimum size.");
        }
        if (file_size > MAX_SCRIPT_FILE_SIZE) {
            throw std::runtime_error("Script File Error: PowerShell script exceeds maximum size limit.");
        }
    }

    [[nodiscard]] OpenFileResult openRegularFileNoFollow(const fs::path& path) {
        // Opening a FIFO for reading normally blocks until a writer connects.
        // Open non-blocking first so that fstat() can reject every non-regular
        // file without allowing an untrusted input path to hang the process.
        // O_NONBLOCK has no effect on subsequent I/O for regular files.
        constexpr int OPEN_FLAGS = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;

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

        const int status_flags = ::fcntl(result.fd.get(), F_GETFL);
        if (status_flags < 0) {
            throwPathError("Failed to get file status flags", path, errno);
        }
        if ((status_flags & O_NONBLOCK) != 0 &&
            ::fcntl(result.fd.get(), F_SETFL, status_flags & ~O_NONBLOCK) != 0) {
            throwPathError("Failed to configure file for reading", path, errno);
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
                validateCoverImageFile(path, file_size);
                return;

            case FileTypeCheck::script_file:
                validateScriptFile(path, file_size);
                return;
        }
        throw std::runtime_error("Internal Error: Unknown file type check.");
    }

    void readExact(const fs::path& path, int fd, std::span<Byte> buffer) {
        while (!buffer.empty()) {
            const auto chunk_size = std::min<std::size_t>(buffer.size(), static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            const auto bytes_read = ::read(fd, buffer.data(), chunk_size);

            if (bytes_read > 0) {
                buffer = buffer.subspan(static_cast<std::size_t>(bytes_read));
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

    void validateJpegSoiMarker(std::span<const Byte> bytes) {
        if (bytes.size() < 2 || bytes[0] != 0xFF || bytes[1] != 0xD8) {
            throw std::runtime_error("File Type Error: Not a valid JPEG file (missing SOI marker).");
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

    if (file_type == FileTypeCheck::cover_image) {
        validateJpegSoiMarker(vec);
    }

    return vec;
}
