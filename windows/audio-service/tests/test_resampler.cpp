#include <cmath>
#include <vector>

#include "smartmic/audio_format.h"
#include "smartmic/resampler.h"
#include "test_harness.h"

using namespace smartmic;

namespace {
// Counts positive-going zero crossings to recover the frequency of a sine.
double measureHz(const std::vector<float>& s, uint32_t rate) {
    size_t crossings = 0;
    size_t firstIdx = 0, lastIdx = 0;
    bool have = false;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i - 1] <= 0.0f && s[i] > 0.0f) {
            if (!have) { firstIdx = i; have = true; }
            lastIdx = i;
            ++crossings;
        }
    }
    if (crossings < 2) return 0.0;
    const double spanSamples = static_cast<double>(lastIdx - firstIdx);
    return static_cast<double>(crossings - 1) * rate / spanSamples;
}
}  // namespace

SM_TEST(A8, "44.1k -> 48k preserves frequency across block boundaries") {
    constexpr uint32_t kIn = 44100;
    constexpr double kHz = 440.0;
    LinearResampler rs(kIn);

    std::vector<float> out;
    // Feed in awkward block sizes so the block boundary is exercised hard.
    const size_t blocks[] = {128, 333, 512, 97, 1024};
    double phase = 0.0;
    const double inc = 2.0 * 3.14159265358979323846 * kHz / kIn;
    for (int rep = 0; rep < 40; ++rep) {
        const size_t n = blocks[static_cast<size_t>(rep) % 5];
        std::vector<float> in(n);
        for (size_t i = 0; i < n; ++i) { in[i] = static_cast<float>(std::sin(phase)); phase += inc; }
        rs.process(in.data(), in.size(), out);
    }

    CHECK(out.size() > 10000);
    CHECK_NEAR(measureHz(out, kSampleRate), kHz, kHz * 0.001);  // within 0.1 %

    // No discontinuity anywhere: a 440 Hz sine at 48 k steps by at most
    // 2*pi*440/48000 ~= 0.0576 per sample. Allow generous headroom; a block
    // seam glitch would be an order of magnitude larger.
    double maxStep = 0.0;
    for (size_t i = 1; i < out.size(); ++i) {
        maxStep = std::max(maxStep, std::fabs(static_cast<double>(out[i] - out[i - 1])));
    }
    CHECK(maxStep < 0.08);
}

SM_TEST(A8b, "48k input is a true passthrough") {
    LinearResampler rs(kSampleRate);
    CHECK(rs.passthrough());
    std::vector<float> in{0.1f, -0.2f, 0.3f};
    std::vector<float> out;
    rs.process(in.data(), in.size(), out);
    CHECK_EQ(out.size(), 3u);
    CHECK_NEAR(out[1], -0.2f, 1e-9);
}
