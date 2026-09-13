// Stateful linear resampler, arbitrary input rate -> canonical 48 kHz mono.
//
// Linear interpolation is deliberately chosen for Phase 1: it is correct,
// allocation-free, and stateful across block boundaries (which is the property
// that actually matters -- see test A8). It is not the final quality bar; a
// polyphase FIR replaces it behind this same interface when measurements say
// so, without touching the router.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace smartmic {

class LinearResampler {
public:
    LinearResampler() = default;
    explicit LinearResampler(uint32_t inputRate) { reset(inputRate); }

    // Safe to call mid-stream: some USB devices change rate on the fly.
    void reset(uint32_t inputRate);

    uint32_t inputRate() const { return inputRate_; }
    bool passthrough() const { return passthrough_; }

    // Number of output samples `inputSamples` will produce (approximate; the
    // resampler is exact and self-correcting, this is for buffer sizing).
    size_t estimateOutput(size_t inputSamples) const;

    // Consumes all of `in`, appends to `out`. Carries fractional phase and the
    // last input sample across calls, so there is no discontinuity at block
    // boundaries.
    void process(const float* in, size_t count, std::vector<float>& out);

private:
    uint32_t inputRate_ = 0;
    bool     passthrough_ = true;
    double   ratio_ = 1.0;   // input samples consumed per output sample
    double   phase_ = 0.0;
    float    last_  = 0.0f;
    bool     primed_ = false;
};

}  // namespace smartmic
