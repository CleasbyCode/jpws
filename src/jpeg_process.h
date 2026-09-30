#pragma once

#include "common.h"

#include <cstddef>
#include <memory>
#include <span>

// True if a JPEG segment of `segment_length` (including the 2-byte length field)
// starting at `pos` lies entirely inside a buffer of `jpg_size` bytes.
[[nodiscard]] constexpr bool jpegSegmentFits(
    std::size_t pos,
    std::size_t segment_length,
    std::size_t jpg_size) noexcept {
    return segment_length >= 2 && pos <= jpg_size && segment_length <= jpg_size - pos;
}

// Strips metadata, applies EXIF orientation, and iteratively resizes
// to eliminate comment-block close sequences "#>" (0x23, 0x3E) from the raw JPEG data.
// The cover is always re-encoded, so on success the image is always modified;
// failure to produce a compatible image is reported by throwing.
void ensureImageCompatible(vBytes& image_file_vec);

enum class TailRetryStatus : std::uint8_t { ready, skip, exhausted };

// Decodes an already-compatible cover once and retains processing buffers across
// progressive JPEG retries. The source bytes need not outlive construction.
class TailRetryGenerator {
public:
    explicit TailRetryGenerator(std::span<const Byte> source_jpg);
    ~TailRetryGenerator();
    TailRetryGenerator(const TailRetryGenerator&) = delete;
    TailRetryGenerator& operator=(const TailRetryGenerator&) = delete;

    // ready: `out` is usable. skip: it contains "#>"; call next again.
    // exhausted: no candidates remain; `out` is unchanged.
    [[nodiscard]] TailRetryStatus next(vBytes& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
