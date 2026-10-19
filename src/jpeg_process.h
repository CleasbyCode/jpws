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
// The cover is always re-encoded, so on success the image is always modified;
// max_encoded_size reserves room for the profile in the final output.
// JPWS_MAX_PIXEL_WORK optionally bounds repeated encoding and resizing.
// By default the full candidate search is available. Failure throws.
void ensureImageCompatible(vBytes& image_file_vec, std::size_t max_encoded_size = MAX_IMAGE_FILE_SIZE);
