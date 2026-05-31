#pragma once

#include "common.h"

#include <span>

struct JpegWarningSummary {
    bool fatal_error = false;
    int extraneous_data = 0;
    int premature_eof = 0;

    [[nodiscard]] bool hasUnsafeTailWarning() const noexcept {
        return fatal_error || extraneous_data > 0 || premature_eof > 0;
    }
};

[[nodiscard]] JpegWarningSummary inspectJpegWarnings(std::span<const Byte> jpg);
