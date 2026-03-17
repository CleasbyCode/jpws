#include "file_utils.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <format>
#include <fcntl.h>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
    class UniqueFd {
    public:
        explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}

        UniqueFd(const UniqueFd&) = delete;
        UniqueFd& operator=(const UniqueFd&) = delete;

        UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

        UniqueFd& operator=(UniqueFd&& other) noexcept {
            if (this != &other) {
                reset(other.release());
            }
            return *this;
        }

        ~UniqueFd() {
            reset();
        }

        [[nodiscard]] int get() const noexcept { return fd_; }

        [[nodiscard]] int release() noexcept {
            const int released_fd = fd_;
            fd_ = -1;
            return released_fd;
        }

        void reset(int new_fd = -1) noexcept {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = new_fd;
        }

    private:
        int fd_;
    };

    struct OpenFileResult {
        UniqueFd fd;
        struct stat status{};
    };

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

            throw std::runtime_error(std::format("Failed to open file: {} ({})", path.string(), std::system_category().message(error_code)));
        }

        if (::fstat(result.fd.get(), &result.status) != 0) {
            const int error_code = errno;
            throw std::runtime_error(std::format("Failed to stat file: {} ({})", path.string(), std::system_category().message(error_code)));
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

    void readExact(const fs::path& path, int fd, std::span<Byte> buffer) {
        std::size_t total_read = 0;

        while (total_read < buffer.size()) {
            const auto remaining = buffer.size() - total_read;
            const auto chunk_size = std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
            auto* buffer_ptr = buffer.data() + static_cast<std::ptrdiff_t>(total_read);

            const auto bytes_read = ::read(fd, buffer_ptr, chunk_size);

            if (bytes_read < 0) {
                if (errno == EINTR) {
                    continue;
                }

                const int error_code = errno;
                throw std::runtime_error(std::format("Failed to read file: {} ({})", path.string(), std::system_category().message(error_code)));
            }

            if (bytes_read == 0) {
                throw std::runtime_error("Failed to read full file: unexpected EOF.");
            }

            total_read += static_cast<std::size_t>(bytes_read);
        }
    }
}

vBytes readFile(const fs::path& path, FileTypeCheck file_type) {
    if (!hasValidFilename(path)) {
        throw std::runtime_error("Invalid Input Error: Unsupported characters in filename arguments.");
    }

    auto open_file = openRegularFileNoFollow(path);
    const std::size_t file_size = fileSizeFromStat(path, open_file.status);

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

    vBytes vec(file_size);
    readExact(path, open_file.fd.get(), vec);

    // Validate JPEG SOI marker to reject non-JPEG files with .jpg extensions.
    if (file_type == FileTypeCheck::cover_image && (vec[0] != 0xFF || vec[1] != 0xD8)) {
        throw std::runtime_error("File Type Error: Not a valid JPEG file (missing SOI marker).");
    }

    return vec;
}
