#pragma once

#include "common.h"

// Strips metadata, applies EXIF orientation, and iteratively resizes
// to eliminate comment-block close sequences "#>" (0x23, 0x3E) from the raw JPEG data.
// Returns true if the image was modified.
[[nodiscard]] bool ensureImageCompatible(vBytes& image_file_vec);
