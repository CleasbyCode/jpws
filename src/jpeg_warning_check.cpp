#include "jpeg_warning_check.h"

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <jerror.h>
#include <jpeglib.h>
#include <limits>
#include <memory>

namespace {
    struct JpegErrorManager {
        jpeg_error_mgr pub{};
        jmp_buf jump{};
        JpegWarningSummary summary{};
        Byte* row = nullptr;
    };

    extern "C" void errorExit(j_common_ptr cinfo) {
        auto* manager = reinterpret_cast<JpegErrorManager*>(cinfo->err);
        manager->summary.fatal_error = true;
        std::free(manager->row);
        manager->row = nullptr;
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
            case JWRN_JPEG_EOF:
                ++manager->summary.premature_eof;
                break;
            default:
                break;
        }
    }
}

JpegWarningSummary inspectJpegWarnings(std::span<const Byte> jpg) {
    if (jpg.empty()) {
        return JpegWarningSummary{ .fatal_error = true };
    }

    // libjpeg reports fatal errors with longjmp. Keep every object that is
    // mutated after setjmp in stable heap storage; otherwise those automatic
    // objects would have indeterminate values after longjmp.
    auto cinfo = std::make_unique<jpeg_decompress_struct>();
    auto manager = std::make_unique<JpegErrorManager>();
    volatile bool decompressor_created = false;

    cinfo->err = jpeg_std_error(&manager->pub);
    manager->pub.error_exit = errorExit;
    manager->pub.emit_message = emitMessage;

    if (setjmp(manager->jump) != 0) {
        if (decompressor_created) {
            jpeg_destroy_decompress(cinfo.get());
        }
        return manager->summary;
    }

    jpeg_create_decompress(cinfo.get());
    decompressor_created = true;

    jpeg_mem_src(cinfo.get(), jpg.data(), static_cast<unsigned long>(jpg.size()));
    (void)jpeg_read_header(cinfo.get(), TRUE);
    (void)jpeg_start_decompress(cinfo.get());

    const auto width = static_cast<std::size_t>(cinfo->output_width);
    const auto components = static_cast<std::size_t>(cinfo->output_components);
    if (width == 0 || components == 0 ||
        width > std::numeric_limits<std::size_t>::max() / components) {
        jpeg_destroy_decompress(cinfo.get());
        return JpegWarningSummary{ .fatal_error = true };
    }

    const std::size_t row_stride = width * components;
    manager->row = static_cast<Byte*>(std::malloc(row_stride));
    if (manager->row == nullptr) {
        jpeg_destroy_decompress(cinfo.get());
        return JpegWarningSummary{ .fatal_error = true };
    }

    while (cinfo->output_scanline < cinfo->output_height) {
        JSAMPROW row_ptr = manager->row;
        (void)jpeg_read_scanlines(cinfo.get(), &row_ptr, 1);
    }

    (void)jpeg_finish_decompress(cinfo.get());
    jpeg_destroy_decompress(cinfo.get());
    std::free(manager->row);
    manager->row = nullptr;

    return manager->summary;
}
