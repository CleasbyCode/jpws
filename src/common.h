#pragma once

// ---------------------------------------------------------------------------
// Toolchain floor. jpws is distributed as source and built by each user, so
// fail early with a clear message rather than a wall of template errors when
// the compiler is too old. The floor is set by std::print / std::format
// (and other C++23 library features used throughout).
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 14
#  error "jpws requires GCC >= 14 (for std::print/std::format). Please upgrade your compiler."
#elif defined(__clang__) && __clang_major__ < 18
#  error "jpws requires Clang >= 18 with a C++23 standard library (libc++ 18+ or libstdc++ 14+). Please upgrade your compiler."
#endif

#include <cstddef>
#include <cstdint>
#include <unistd.h>
#include <vector>

constexpr std::size_t ICC_PROFILE_TEMPLATE_SIZE = 430;
constexpr std::size_t MAX_PROFILE_SEGMENT_SIZE = 10 * 1024;
constexpr std::size_t MAX_SCRIPT_FILE_SIZE = MAX_PROFILE_SEGMENT_SIZE + 2 - ICC_PROFILE_TEMPLATE_SIZE;

using Byte   = std::uint8_t;
using vBytes = std::vector<Byte>;

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}

    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
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
