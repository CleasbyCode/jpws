// Independent integration-test decoder: fail on every libjpeg warning or
// error, including recoverable scan corruption that ffmpeg may accept.
#include <csetjmp>
#include <cstdio>
#include <limits>
#include <memory>

#include <jpeglib.h>

namespace {
    struct FileCloser {
        void operator()(FILE* file) const noexcept {
            (void)std::fclose(file);
        }
    };

    struct Decoder {
        jpeg_decompress_struct info{};
        jpeg_error_mgr errors{};
        jmp_buf jump{};
        bool created = false;
    };

    extern "C" void failDecode(j_common_ptr info) {
        info->err->output_message(info);
        auto* decoder = static_cast<Decoder*>(info->client_data);
        longjmp(decoder->jump, 1);
    }

    extern "C" void rejectWarning(j_common_ptr info, int level) {
        if (level < 0) {
            failDecode(info);
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: jpeg_decode_check JPEG\n");
        return 2;
    }

    const std::unique_ptr<FILE, FileCloser> input(std::fopen(argv[1], "rb"));
    if (!input) {
        std::perror(argv[1]);
        return 2;
    }

    // All state modified between setjmp and longjmp lives on the heap.
    const auto decoder = std::make_unique<Decoder>();
    decoder->info.err = jpeg_std_error(&decoder->errors);
    decoder->errors.error_exit = failDecode;
    decoder->errors.emit_message = rejectWarning;
    decoder->info.client_data = decoder.get();
    if (setjmp(decoder->jump) != 0) {
        if (decoder->created) {
            jpeg_destroy_decompress(&decoder->info);
        }
        return 1;
    }

    jpeg_create_decompress(&decoder->info);
    decoder->created = true;
    jpeg_stdio_src(&decoder->info, input.get());
    (void)jpeg_read_header(&decoder->info, TRUE);
    (void)jpeg_start_decompress(&decoder->info);

    const auto components = static_cast<JDIMENSION>(decoder->info.output_components);
    if (components == 0 || decoder->info.output_width >
            std::numeric_limits<JDIMENSION>::max() / components) {
        std::fprintf(stderr, "Invalid decoded JPEG row size\n");
        jpeg_destroy_decompress(&decoder->info);
        return 1;
    }
    const JDIMENSION row_size = decoder->info.output_width * components;
    const JSAMPARRAY row = decoder->info.mem->alloc_sarray(
        reinterpret_cast<j_common_ptr>(&decoder->info), JPOOL_IMAGE, row_size, 1);
    while (decoder->info.output_scanline < decoder->info.output_height) {
        if (jpeg_read_scanlines(&decoder->info, row, 1) != 1) {
            std::fprintf(stderr, "JPEG decoder did not produce a complete row\n");
            jpeg_destroy_decompress(&decoder->info);
            return 1;
        }
    }

    const bool finished = jpeg_finish_decompress(&decoder->info) != FALSE;
    jpeg_destroy_decompress(&decoder->info);
    return finished ? 0 : 1;
}
