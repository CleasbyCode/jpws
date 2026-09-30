#include "jpeg_warning_check.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <jpeglib.h>
#include <vector>

namespace {
    int failures = 0;

    void expect(bool condition, const char* expression, int line) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
            ++failures;
        }
    }

#define EXPECT_WARNING(condition) expect(static_cast<bool>(condition), #condition, __LINE__)

    vBytes makeGradientJpeg() {
        constexpr JDIMENSION SIDE = 408;
        constexpr std::size_t COMPONENTS = 3;

        jpeg_compress_struct compressor{};
        jpeg_error_mgr error_manager{};
        compressor.err = jpeg_std_error(&error_manager);
        jpeg_create_compress(&compressor);

        unsigned char* encoded = nullptr;
        unsigned long encoded_size = 0;
        jpeg_mem_dest(&compressor, &encoded, &encoded_size);

        compressor.image_width = SIDE;
        compressor.image_height = SIDE;
        compressor.input_components = static_cast<int>(COMPONENTS);
        compressor.in_color_space = JCS_RGB;
        jpeg_set_defaults(&compressor);
        for (int component = 0; component < compressor.num_components; ++component) {
            compressor.comp_info[component].h_samp_factor = 1;
            compressor.comp_info[component].v_samp_factor = 1;
        }
        jpeg_set_quality(&compressor, 90, TRUE);
        jpeg_simple_progression(&compressor);
        jpeg_start_compress(&compressor, TRUE);

        std::vector<JSAMPLE> row(static_cast<std::size_t>(SIDE) * COMPONENTS);
        while (compressor.next_scanline < compressor.image_height) {
            for (std::size_t x = 0; x < SIDE; ++x) {
                row[x * COMPONENTS] = static_cast<JSAMPLE>(x % 256);
                row[x * COMPONENTS + 1] = static_cast<JSAMPLE>(compressor.next_scanline % 256);
                row[x * COMPONENTS + 2] = static_cast<JSAMPLE>((x + compressor.next_scanline) % 256);
            }
            JSAMPROW row_pointer = row.data();
            (void)jpeg_write_scanlines(&compressor, &row_pointer, 1);
        }

        jpeg_finish_compress(&compressor);
        vBytes result(encoded, encoded + encoded_size);
        jpeg_destroy_compress(&compressor);
        std::free(encoded);
        return result;
    }
}

int runJpegWarningTests() {
    failures = 0;
    const vBytes original = makeGradientJpeg();
    EXPECT_WARNING(!inspectJpegWarnings(original).hasUnsafeTailWarning());

    // This scan is clean before replacing the ten bytes immediately before
    // EOI. The patch produces JWRN_HIT_MARKER ("premature end of data
    // segment"), which used to be discarded by the warning inspector.
    vBytes patched = original;
    constexpr std::array<Byte, 10> DEFAULT_TAIL{
        0x9E, 0x23, 0x3E, 0x0D, 0x23, 0x00, 0x00, 0x20, 0x20, 0x00
    };
    std::copy(DEFAULT_TAIL.begin(), DEFAULT_TAIL.end(), patched.end() - 12);
    const JpegWarningSummary patched_summary = inspectJpegWarnings(patched);
    EXPECT_WARNING(!patched_summary.fatal_error);
    EXPECT_WARNING(patched_summary.extraneous_data == 0);
    EXPECT_WARNING(patched_summary.premature_eof == 0);
    EXPECT_WARNING(patched_summary.other_warning);
    EXPECT_WARNING(patched_summary.hasUnsafeTailWarning());

    // Missing the actual EOI marker remains a separately counted warning.
    vBytes truncated = original;
    truncated.resize(truncated.size() - 2);
    const JpegWarningSummary truncated_summary = inspectJpegWarnings(truncated);
    EXPECT_WARNING(truncated_summary.premature_eof > 0);
    EXPECT_WARNING(truncated_summary.hasUnsafeTailWarning());
    return failures;
}
