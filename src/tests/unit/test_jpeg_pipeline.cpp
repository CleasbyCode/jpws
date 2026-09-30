// Compile this translation unit instead of a separate jpeg_process.cpp in the
// unit test binary so internal image transforms can be checked without exposing
// test-only production APIs or compiling the STB implementation twice.
#pragma GCC diagnostic push
// GCC treats this .cpp inclusion as a header and warns about the private pimpl's
// local types. The implementation is still defined in exactly one translation unit.
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#include "../../jpeg_process.cpp"
#pragma GCC diagnostic pop

#include <cstdlib>

namespace {
    int pipeline_failures = 0;

    void checkPipeline(bool condition, const char* expression, int line) {
        if (!condition) {
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, line, expression);
            ++pipeline_failures;
        }
    }

#define PIPELINE_EXPECT(condition) checkPipeline(static_cast<bool>(condition), #condition, __LINE__)

    DecodedImage gradientImage(ImageSize size) {
        DecodedImage result{size, vBytes(checkedPixelBufferSize(size.width, size.height, 3))};
        for (int y = 0; y < size.height; ++y) {
            for (int x = 0; x < size.width; ++x) {
                const Byte value = static_cast<Byte>(20 + x * 100 / size.width + y * 80 / size.height);
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                                     static_cast<std::size_t>(x)) * 3;
                std::fill_n(result.pixels.data() + offset, 3, value);
            }
        }
        return result;
    }

    void testAllOrientationMappings() {
        PIPELINE_EXPECT(transformIsPerfect(TJXOP_NONE, {400, 410}, -1));
        PIPELINE_EXPECT(transformIsPerfect(TJXOP_TRANSPOSE, {400, 410}, -1));
        PIPELINE_EXPECT(!transformIsPerfect(TJXOP_HFLIP, {400, 410}, -1));
        const std::array<std::array<Byte, 6>, 8> expected{{
            {{1, 2, 3, 4, 5, 6}}, {{3, 2, 1, 6, 5, 4}},
            {{6, 5, 4, 3, 2, 1}}, {{4, 5, 6, 1, 2, 3}},
            {{1, 4, 2, 5, 3, 6}}, {{4, 1, 5, 2, 6, 3}},
            {{6, 3, 5, 2, 4, 1}}, {{3, 6, 2, 5, 1, 4}}
        }};
        DecodedImage source{{3, 2}, {}};
        for (Byte pixel = 1; pixel <= 6; ++pixel) {
            source.pixels.insert(source.pixels.end(), 3, pixel);
        }
        for (uint16_t orientation = 1; orientation <= 8; ++orientation) {
            const DecodedImage oriented = orientPixels(source, getTransformOp(orientation));
            PIPELINE_EXPECT(oriented.size.width == (orientation >= 5 ? 2 : 3));
            PIPELINE_EXPECT(oriented.size.height == (orientation >= 5 ? 3 : 2));
            for (std::size_t pixel = 0; pixel < 6; ++pixel) {
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    PIPELINE_EXPECT(oriented.pixels[pixel * 3 + channel] == expected[orientation - 1][pixel]);
                }
            }
        }
    }

    void testExifFallbackPreservesMinimumAndEdgePixels() {
        const DecodedImage source = gradientImage({400, 410});
        JpegEncoder encoder;
        vBytes jpg;
        const EncodeCandidate subsampled{TJSAMP_411, PROGRESSIVE_JPEG_FLAGS, "4:1:1"};
        compressPixelsToJpeg(jpg, source.pixels, source.size, encoder, 97, subsampled);
        // Little-endian EXIF Orientation=2 (horizontal reflection). A lossless
        // 4:1:1 transform previously trimmed width 400 down to 384.
        const vBytes exif{
            0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
            'I', 'I', 0x2A, 0, 8, 0, 0, 0, 1, 0,
            0x12, 1, 3, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
        };
        jpg.insert(jpg.begin() + 2, exif.begin(), exif.end());
        ensureImageCompatible(jpg);  // void: returns on success, throws on failure
        PIPELINE_EXPECT(!exifOrientation(jpg));
        const DecodedImage transformed = decodeJpeg(jpg);
        PIPELINE_EXPECT(transformed.size.width == 400);
        PIPELINE_EXPECT(transformed.size.height == 410);
        for (const int y : {0, 100, 409}) {
            for (const int x : {0, 50, 399}) {
                const auto actual = static_cast<std::size_t>((y * 400 + x) * 3);
                const auto original = static_cast<std::size_t>((y * 400 + 399 - x) * 3);
                PIPELINE_EXPECT(std::abs(static_cast<int>(transformed.pixels[actual]) -
                                         static_cast<int>(source.pixels[original])) <= 4);
            }
        }
    }

    void testAspectRatioAndMinimum() {
        JpegEncoder encoder;
        vBytes scratch;
        vBytes jpg;
        auto decompressor = makeHandle(tjInitDecompress(), "tjInitDecompress()");
        for (const ImageSize original : {ImageSize{1200, 700}, ImageSize{700, 1200}}) {
            for (const int decrease : {1, 222, 300}) {
                const auto resized = resizedImageSize(original, decrease);
                PIPELINE_EXPECT(resized.has_value());
                if (!resized) {
                    continue;
                }
                PIPELINE_EXPECT(std::min(resized->width, resized->height) == 700 - decrease);
                PIPELINE_EXPECT(std::abs(resized->width * original.height - resized->height * original.width) <= 350);
            }
            PIPELINE_EXPECT(!resizedImageSize(original, 301));
            const DecodedImage source = gradientImage(original);
            resizeImage(jpg, source, scratch, encoder, 90, 300, encodeCandidates(90).front());
            const ImageSize actual = readJpegSize(decompressor.get(), jpg, "test resized header");
            PIPELINE_EXPECT(actual.width == (original.width > original.height ? 686 : 400));
            PIPELINE_EXPECT(actual.height == (original.width > original.height ? 400 : 686));
        }
        PIPELINE_EXPECT(!resizedImageSize({400, 410}, 1));
    }

    void testDistinctModesRetainAllPreviousCandidates() {
        // A small textured fixture makes DCT differences visible while keeping
        // all-quality coverage inexpensive. Compare against the old three-mode
        // search so dropping duplicate settings cannot lose an effective mode.
        DecodedImage source{{32, 24}, vBytes(32 * 24 * 3)};
        uint32_t state = 7;
        for (Byte& value : source.pixels) {
            state = state * 1664525U + 1013904223U;
            value = static_cast<Byte>(state >> 24);
        }
        JpegEncoder encoder;
        int count = 0;
        for (int quality = 97; quality >= 75; --quality) {
            std::vector<vBytes> retained;
            for (const EncodeCandidate& candidate : encodeCandidates(quality)) {
                vBytes jpg;
                compressPixelsToJpeg(jpg, source.pixels, source.size, encoder, quality, candidate);
                PIPELINE_EXPECT(std::ranges::find(retained, jpg) == retained.end());
                retained.push_back(std::move(jpg));
                ++count;
            }
            for (const int flag : {0, TJFLAG_ACCURATEDCT, TJFLAG_FASTDCT}) {
                vBytes old_candidate;
                const EncodeCandidate candidate{TJSAMP_444, PROGRESSIVE_JPEG_FLAGS | flag, "previous mode"};
                compressPixelsToJpeg(old_candidate, source.pixels, source.size, encoder, quality, candidate);
                PIPELINE_EXPECT(std::ranges::find(retained, old_candidate) != retained.end());
            }
        }
        PIPELINE_EXPECT(count == 44);
    }

    void testRetryOwnsSourceAndExhaustsAtMinimum() {
        const DecodedImage source = gradientImage({802, 402});
        JpegEncoder encoder;
        vBytes original;
        compressPixelsToJpeg(original, source.pixels, source.size, encoder, 90, encodeCandidates(90).front());
        TailRetryGenerator retries(original);
        original.assign(1, 0); // Source lifetime must not affect later retries.

        auto decompressor = makeHandle(tjInitDecompress(), "tjInitDecompress()");
        vBytes candidate;
        for (int index = 0; index < 46; ++index) {
            const auto status = retries.next(candidate);
            PIPELINE_EXPECT(status == TailRetryStatus::ready || status == TailRetryStatus::skip);
            PIPELINE_EXPECT((status == TailRetryStatus::skip) == containsCommentBlockClose(candidate));
            const ImageSize size = readJpegSize(decompressor.get(), candidate, "test retry header");
            PIPELINE_EXPECT(size.width == (index < 44 ? 802 : index == 44 ? 800 : 798));
            PIPELINE_EXPECT(size.height == (index < 44 ? 402 : index == 44 ? 401 : 400));
        }
        const vBytes last = candidate;
        PIPELINE_EXPECT(retries.next(candidate) == TailRetryStatus::exhausted);
        PIPELINE_EXPECT(retries.next(candidate) == TailRetryStatus::exhausted);
        PIPELINE_EXPECT(candidate == last);
    }
}

int runJpegPipelineTests() {
    testAllOrientationMappings();
    testExifFallbackPreservesMinimumAndEdgePixels();
    testAspectRatioAndMinimum();
    testDistinctModesRetainAllPreviousCandidates();
    testRetryOwnsSourceAndExhaustsAtMinimum();
    return pipeline_failures;
}
