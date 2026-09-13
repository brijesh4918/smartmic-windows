#include "smartmic/resampler.h"

#include <cmath>

#include "smartmic/audio_format.h"

namespace smartmic {

void LinearResampler::reset(uint32_t inputRate) {
    inputRate_ = inputRate;
    passthrough_ = (inputRate == kSampleRate) || inputRate == 0;
    ratio_ = passthrough_ ? 1.0 : static_cast<double>(inputRate) / static_cast<double>(kSampleRate);
    phase_ = 0.0;
    last_ = 0.0f;
    primed_ = false;
}

size_t LinearResampler::estimateOutput(size_t inputSamples) const {
    if (passthrough_) return inputSamples;
    return static_cast<size_t>(std::ceil(inputSamples / ratio_)) + 1;
}

void LinearResampler::process(const float* in, size_t count, std::vector<float>& out) {
    if (count == 0) return;
    if (passthrough_) {
        out.insert(out.end(), in, in + count);
        return;
    }
    out.reserve(out.size() + estimateOutput(count));

    // `phase_` is the fractional read position within the current input block,
    // where position -1 refers to `last_` (the final sample of the previous
    // block). Carrying both across calls is what removes the block-boundary
    // discontinuity that a stateless resampler produces.
    double pos = phase_;
    while (true) {
        const double floorPos = std::floor(pos);
        const auto i = static_cast<long long>(floorPos);
        if (i + 1 >= static_cast<long long>(count)) break;
        const float a = (i < 0) ? (primed_ ? last_ : in[0]) : in[static_cast<size_t>(i)];
        const float b = in[static_cast<size_t>(i + 1)];
        const auto frac = static_cast<float>(pos - floorPos);
        out.push_back(a + (b - a) * frac);
        pos += ratio_;
    }
    last_ = in[count - 1];
    primed_ = true;
    phase_ = pos - static_cast<double>(count);  // carry, relative to next block
}

}  // namespace smartmic
