// Exercise the serialized profile produced by the application, including every
// allowed payload length. Keep these implementation details out of its API.
#define main jpwsApplicationMainForTests
#include "../../jpws.cpp"
#undef main

#include <cstdio>

int runProfileTests() {
    constexpr std::size_t SCRIPT_OFFSET = 424;
    constexpr std::size_t EXPECTED_CLOSE_OFFSET = 418;
    constexpr std::array<Byte, 6> TRAILER{ '\r', '\n', '<', '#', '\r', '\n' };
    int failures = 0;

    for (std::size_t length = 10; length <= MAX_SCRIPT_FILE_SIZE; ++length) {
        const vBytes script(length, 'A');
        const vBytes profile = buildProfilePayload(script);
        const auto bytes = std::span<const Byte>(profile);
        const auto first_close = std::ranges::search(bytes, PROFILE_COMMENT_CLOSE);
        const std::size_t segment_length =
            (static_cast<std::size_t>(profile[2]) << 8) | profile[3];
        std::size_t profile_length = 0;
        for (std::size_t index = 18; index < 22; ++index) {
            profile_length = (profile_length << 8) | profile[index];
        }
        const std::size_t expected_padding = (length == 8594 || length == 8610) ? 1 : 0;

        const bool valid =
            static_cast<std::size_t>(first_close.begin() - bytes.begin()) == EXPECTED_CLOSE_OFFSET &&
            segment_length == profile.size() - 2 &&
            segment_length <= MAX_PROFILE_SEGMENT_SIZE &&
            profile_length == profile.size() - 18 &&
            profile.size() == ICC_PROFILE_TEMPLATE_SIZE + script.size() + expected_padding &&
            std::ranges::equal(bytes.subspan(SCRIPT_OFFSET, script.size()), script) &&
            std::ranges::equal(bytes.subspan(SCRIPT_OFFSET + script.size(), TRAILER.size()), TRAILER) &&
            (expected_padding == 0 || profile.back() == 0);
        if (!valid) {
            std::fprintf(stderr, "Invalid serialized ICC profile for %zu-byte script\n", length);
            ++failures;
            break;
        }
    }

    try {
        (void)buildProfilePayload(vBytes(MAX_SCRIPT_FILE_SIZE + 1, 'A'));
        std::fprintf(stderr, "Oversized script was accepted by profile builder\n");
        ++failures;
    }
    catch (const std::runtime_error&) {
    }
    return failures;
}

int runTailSafetyTests(const vBytes& flat_cover) {
    int failures = 0;
    const auto expect = [&](bool ok) {
        if (!ok) { std::fprintf(stderr, "Tail/output safety regression failed\n"); ++failures; }
    };
    const vBytes profile = buildProfilePayload(vBytes(10, 'A'));
    for (const auto option : {Option::None, Option::Alt}) {
        vBytes candidate = flat_cover;
        bool rejected = false;
        try { patchImageTail(candidate, option); }
        catch (const std::runtime_error&) { rejected = true; }
        expect(rejected && candidate == flat_cover);
        rejected = false;
        try { (void)buildValidatedOutput(flat_cover, profile, option); }
        catch (const std::runtime_error&) { rejected = true; }
        expect(rejected);
    }

    // Marker-walking boundaries: APP payloads may contain fake SOS/EOI;
    // entropy stuffing is data, but restart markers must not be overwritten.
    vBytes scan{0xFF,0xD8, 0xFF,0xE1,0,6,0xFF,0xDA,0xFF,0xD9,
                0xFF,0xDA,0,2};
    scan.insert(scan.end(), 10, 0x11);
    scan.insert(scan.end(), {0xFF,0xD9});
    expect(tailPatchFits(scan, 10));
    expect(!tailPatchFits(scan, 11));
    auto stuffed = scan;
    stuffed[14] = 0xFF; stuffed[15] = 0;
    expect(tailPatchFits(stuffed, 10));
    expect(!tailPatchFits(stuffed, 9));
    auto restart = scan;
    restart[14] = 0xFF; restart[15] = 0xD0;
    expect(!tailPatchFits(restart, 10));
    expect(tailPatchFits(restart, 8));
    auto truncated = scan;
    truncated.pop_back();
    expect(!tailPatchFits(truncated, 10));

    // A patchable tail alone is insufficient: this has no frame header.
    // Build a valid APP0-sized prefix so assembly does not split a marker.
    vBytes invalid{0xFF,0xD8,0xFF,0xE0,0,16};
    invalid.resize(20, 0);
    invalid.insert(invalid.end(), scan.begin() + 10, scan.end());
    expect(tailPatchFits(invalid, 10));
    bool rejected = false;
    try { (void)buildValidatedOutput(invalid, profile, Option::None); }
    catch (const std::runtime_error& e) {
        rejected = std::string_view(e.what()).find("cannot be decoded") != std::string_view::npos;
    }
    expect(rejected);

    vBytes large(MAX_IMAGE_FILE_SIZE - profile.size(), 0);
    expect(buildPolyglotOutputBytes(large, profile).size() == MAX_IMAGE_FILE_SIZE);
    large.push_back(0);
    rejected = false;
    try { (void)buildPolyglotOutputBytes(large, profile); }
    catch (const std::runtime_error&) { rejected = true; }
    expect(rejected);
    return failures;
}
