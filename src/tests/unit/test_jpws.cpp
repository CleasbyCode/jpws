#include "jpeg_process.h"
#include "jpeg_warning_check.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <jpeglib.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {
    int g_failures = 0;

    void expect(bool cond, const char* expr, const char* file, int line) {
        if (!cond) {
            std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
            ++g_failures;
        }
    }

#define EXPECT(cond) expect(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

    vBytes readAll(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "unable to open %s\n", path.c_str());
            std::exit(2);
        }
        return vBytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    vBytes makeCmykJpeg() {
        constexpr JDIMENSION WIDTH = 400;
        constexpr JDIMENSION HEIGHT = 400;
        constexpr std::size_t COMPONENTS = 4;

        jpeg_compress_struct cinfo{};
        jpeg_error_mgr error_manager{};
        cinfo.err = jpeg_std_error(&error_manager);
        jpeg_create_compress(&cinfo);

        unsigned char* encoded = nullptr;
        unsigned long encoded_size = 0;
        jpeg_mem_dest(&cinfo, &encoded, &encoded_size);

        cinfo.image_width = WIDTH;
        cinfo.image_height = HEIGHT;
        cinfo.input_components = static_cast<int>(COMPONENTS);
        cinfo.in_color_space = JCS_CMYK;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        std::vector<JSAMPLE> row(static_cast<std::size_t>(WIDTH) * COMPONENTS);
        for (std::size_t x = 0; x < static_cast<std::size_t>(WIDTH); ++x) {
            row[x * COMPONENTS] = static_cast<JSAMPLE>(x % 256);
            row[x * COMPONENTS + 1] = static_cast<JSAMPLE>((x * 3) % 256);
            row[x * COMPONENTS + 2] = static_cast<JSAMPLE>((x * 7) % 256);
            row[x * COMPONENTS + 3] = 32;
        }

        while (cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row_ptr = row.data();
            (void)jpeg_write_scanlines(&cinfo, &row_ptr, 1);
        }

        jpeg_finish_compress(&cinfo);
        vBytes result(encoded, encoded + encoded_size);
        jpeg_destroy_compress(&cinfo);
        std::free(encoded);
        return result;
    }
}

static void test_jpeg_segment_fits_rejects_length_past_buffer() {
    // Classic underflow: 16-bit max length vs a 100-byte JPEG. The old
    // `pos > size - length` form wraps and incorrectly accepts this.
    EXPECT(!jpegSegmentFits(4, 65535, 100));
    EXPECT(!jpegSegmentFits(2, 11, 12));
    EXPECT(!jpegSegmentFits(13, 2, 12));
    EXPECT(!jpegSegmentFits(2, 1, 100));
    EXPECT(jpegSegmentFits(2, 2, 4));
    EXPECT(jpegSegmentFits(2, 10, 12));
}

static fs::path testdataCover() {
    return fs::path(__FILE__).parent_path().parent_path() / "testdata" / "covers" / "cover.jpg";
}

static void test_inspect_jpeg_warnings_fatal_truncated_cover() {
    vBytes jpg = readAll(testdataCover());
    EXPECT(jpg.size() > 256);
    jpg.resize(jpg.size() / 4);
    const JpegWarningSummary summary = inspectJpegWarnings(jpg);
    EXPECT(summary.hasUnsafeTailWarning());
}

static void test_inspect_jpeg_warnings_empty_is_fatal() {
    const JpegWarningSummary summary = inspectJpegWarnings({});
    EXPECT(summary.fatal_error);
    EXPECT(summary.hasUnsafeTailWarning());
}

static void test_tail_retry_distinguishes_exhausted_from_candidate() {
    const vBytes cover = readAll(testdataCover());
    vBytes out;

    EXPECT(makeTailRetryCandidate(cover, out, -1) == TailRetryStatus::exhausted);

    // 69 same-dimension candidates + 300 resize attempts; 10'000 is well past that.
    EXPECT(makeTailRetryCandidate(cover, out, 10000) == TailRetryStatus::exhausted);

    const TailRetryStatus first = makeTailRetryCandidate(cover, out, 0);
    EXPECT(first == TailRetryStatus::ready || first == TailRetryStatus::skip);
    EXPECT(first != TailRetryStatus::exhausted);
}

static void test_compatible_header_cannot_bypass_canonicalization() {
    vBytes jpg = readAll(testdataCover());
    EXPECT(jpg.size() >= 20);

    // This was the old externally forgeable "already compatible" signature.
    jpg[11] = 0x01;
    jpg[12] = 0x01;
    jpg[13] = 0x19;
    constexpr std::string_view TRAILER = "TRAILING-DATA";
    jpg.insert(jpg.end(), TRAILER.begin(), TRAILER.end());

    EXPECT(ensureImageCompatible(jpg));
    EXPECT(jpg.size() >= 2);
    EXPECT(jpg[jpg.size() - 2] == 0xFF);
    EXPECT(jpg[jpg.size() - 1] == 0xD9);
}

static void test_cmyk_cover_is_rejected_before_processing() {
    vBytes jpg = makeCmykJpeg();
    const vBytes original = jpg;
    bool rejected_for_colorspace = false;

    try {
        (void)ensureImageCompatible(jpg);
    }
    catch (const std::runtime_error& error) {
        const std::string_view message(error.what());
        rejected_for_colorspace = message.find("color") != std::string_view::npos ||
                                  message.find("Color") != std::string_view::npos ||
                                  message.find("CMYK") != std::string_view::npos;
    }

    EXPECT(rejected_for_colorspace);
    EXPECT(jpg == original);
}

int main() {
    test_jpeg_segment_fits_rejects_length_past_buffer();
    test_inspect_jpeg_warnings_empty_is_fatal();
    test_inspect_jpeg_warnings_fatal_truncated_cover();
    test_tail_retry_distinguishes_exhausted_from_candidate();
    test_compatible_header_cannot_bypass_canonicalization();
    test_cmyk_cover_is_rejected_before_processing();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("ok");
    return 0;
}
