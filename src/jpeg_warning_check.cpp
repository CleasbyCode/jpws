#include "jpeg_warning_check.h"

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <jerror.h>
#include <jpeglib.h>
#include <vector>

namespace {
    struct JpegErrorManager {
        jpeg_error_mgr pub{};
        jmp_buf jump{};
        JpegWarningSummary summary{};
    };

    extern "C" void errorExit(j_common_ptr cinfo) {
        auto* manager = reinterpret_cast<JpegErrorManager*>(cinfo->err);
        manager->summary.fatal_error = true;
        longjmp(manager->jump, 1);
    }

    extern "C" void emitMessage(j_common_ptr cinfo, int msg_level) {
        if (msg_level >= 0) {
            return;
        }

        auto* manager = reinterpret_cast<JpegErrorManager*>(cinfo->err);
        switch (cinfo->err->msg_code) {
            case JWRN_EXTRANEOUS_DATA:
                ++manager->summary.extraneous_data;
                break;
            case JWRN_HIT_MARKER:
                ++manager->summary.premature_data_segment;
                break;
            case JWRN_JPEG_EOF:
                ++manager->summary.premature_eof;
                break;
            default:
                ++manager->summary.other_warnings;
                break;
        }
    }
}

JpegWarningSummary inspectJpegWarnings(std::span<const Byte> jpg) {
    if (jpg.empty()) {
        return JpegWarningSummary{ .fatal_error = true };
    }

    jpeg_decompress_struct cinfo{};
    JpegErrorManager manager{};
    bool decompressor_created = false;

    cinfo.err = jpeg_std_error(&manager.pub);
    manager.pub.error_exit = errorExit;
    manager.pub.emit_message = emitMessage;

    if (setjmp(manager.jump) != 0) {
        if (decompressor_created) {
            jpeg_destroy_decompress(&cinfo);
        }
        return manager.summary;
    }

    jpeg_create_decompress(&cinfo);
    decompressor_created = true;

    jpeg_mem_src(&cinfo, jpg.data(), jpg.size());
    (void)jpeg_read_header(&cinfo, TRUE);
    (void)jpeg_start_decompress(&cinfo);

    const std::size_t row_stride = static_cast<std::size_t>(cinfo.output_width) *
                                   static_cast<std::size_t>(cinfo.output_components);
    std::vector<Byte> row(row_stride);

    while (cinfo.output_scanline < cinfo.output_height) {
        Byte* row_ptr = row.data();
        (void)jpeg_read_scanlines(&cinfo, &row_ptr, 1);
    }

    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return manager.summary;
}
