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
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

constexpr std::size_t ICC_PROFILE_TEMPLATE_SIZE = 430;
constexpr std::size_t MAX_PROFILE_SEGMENT_SIZE = 10 * 1024;

// The script is embedded inside the ICC profile segment, which is capped at
// MAX_PROFILE_SEGMENT_SIZE. The segment size field is measured from just after
// the 2-byte length field, so the full segment is (segment_size + 2) bytes; the
// fixed ICC_PROFILE_TEMPLATE contributes ICC_PROFILE_TEMPLATE_SIZE of that. What
// remains is the largest script that still fits: (10240 + 2) - 430 = 9812 bytes.
// NOTE: this leaves no headroom for the profile-size-field padding in
// buildProfilePayload (jpws.cpp); that is safe only because the byte patterns
// that trigger padding occur at fixed sizes well below this maximum. See the
// comment on that padding loop before changing this value or the template.
constexpr std::size_t MAX_SCRIPT_FILE_SIZE = MAX_PROFILE_SEGMENT_SIZE + 2 - ICC_PROFILE_TEMPLATE_SIZE;

using Byte   = std::uint8_t;
using vBytes = std::vector<Byte>;

// Neutralize control characters (C0 range and DEL) in untrusted text before it
// is embedded in a diagnostic. A crafted filename or argv[0] can otherwise smuggle
// terminal escape sequences (e.g. ESC 0x1B) into the user's terminal via stderr.
// Bytes >= 0x80 are preserved so multi-byte UTF-8 filenames display intact.
[[nodiscard]] inline std::string sanitizeForDisplay(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        out.push_back((c < 0x20 || c == 0x7F) ? '?' : ch);
    }
    return out;
}

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
