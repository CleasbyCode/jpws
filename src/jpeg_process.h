#pragma once

#include "common.h"

#include <span>

// Strips metadata, applies EXIF orientation, and iteratively resizes
// to eliminate comment-block close sequences "#>" (0x23, 0x3E) from the raw JPEG data.
// Returns true if the image was modified.
[[nodiscard]] bool ensureImageCompatible(vBytes& image_file_vec);

// Produces another progressive JPEG candidate from an already-compatible cover image.
// Returns false if the selected candidate still contains a "#>" sequence.
[[nodiscard]] bool makeTailRetryCandidate(std::span<const Byte> source_jpg, vBytes& out, int retry_index);
