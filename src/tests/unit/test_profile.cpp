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
