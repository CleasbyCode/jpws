#pragma once

#include "common.h"

#include <cstddef>
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
// Returns true if the image was modified.
[[nodiscard]] bool ensureImageCompatible(vBytes& image_file_vec);

enum class TailRetryStatus : std::uint8_t { ready, skip, exhausted };

// Produces another progressive JPEG candidate from an already-compatible cover image.
// ready: `out` is usable. skip: this index still contains "#>"; try the next index.
// exhausted: retry_index is out of range; `out` is unchanged.
[[nodiscard]] TailRetryStatus makeTailRetryCandidate(
    std::span<const Byte> source_jpg,
    vBytes& out,
    int retry_index);
