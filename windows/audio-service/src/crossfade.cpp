#include "smartmic/crossfade.h"

#include <algorithm>
#include <cmath>

#include "smartmic/audio_format.h"

namespace smartmic {
namespace {
constexpr float kHalfPi = 1.57079632679489661923f;
}

void Crossfade::fadeTo(float target, uint32_t durationMs) {
    target_ = std::clamp(target, 0.0f, 1.0f);
    const float distance = target_ - position_;
    if (distance == 0.0f) {
        remaining_ = 0;
        step_ = 0.0f;
        return;
    }
    // Duration is for a full 0->1 traversal; a retarget mid-fade covers only
    // the remaining distance, at the same rate, so the slope never jumps.
    const size_t fullSamples = std::max<size_t>(1, millisToSamples(durationMs));
    const size_t samples = std::max<size_t>(1, static_cast<size_t>(std::ceil(fullSamples * std::fabs(distance))));
    remaining_ = samples;
    step_ = distance / static_cast<float>(samples);
}

void Crossfade::snapTo(float target) {
    position_ = std::clamp(target, 0.0f, 1.0f);
    target_ = position_;
    remaining_ = 0;
    step_ = 0.0f;
}

GainPair Crossfade::next() {
    if (remaining_ > 0) {
        position_ += step_;
        if (--remaining_ == 0) position_ = target_;  // land exactly, no drift
        position_ = std::clamp(position_, 0.0f, 1.0f);
    }
    const float a = position_ * kHalfPi;
    return GainPair{std::cos(a), std::sin(a)};
}

void GainRamp::rampTo(float target, uint32_t durationMs) {
    target_ = std::clamp(target, 0.0f, 1.0f);
    const float distance = target_ - value_;
    if (distance == 0.0f) {
        remaining_ = 0;
        step_ = 0.0f;
        return;
    }
    const size_t fullSamples = std::max<size_t>(1, millisToSamples(durationMs));
    const size_t samples = std::max<size_t>(1, static_cast<size_t>(std::ceil(fullSamples * std::fabs(distance))));
    remaining_ = samples;
    step_ = distance / static_cast<float>(samples);
}

void GainRamp::snapTo(float target) {
    value_ = std::clamp(target, 0.0f, 1.0f);
    target_ = value_;
    remaining_ = 0;
    step_ = 0.0f;
}

float GainRamp::next() {
    if (remaining_ > 0) {
        value_ += step_;
        if (--remaining_ == 0) value_ = target_;
        value_ = std::clamp(value_, 0.0f, 1.0f);
    }
    // Raised cosine, so the gate leaves and reaches its endpoints with zero
    // slope instead of a corner.
    return 0.5f - 0.5f * std::cos(value_ * 2.0f * kHalfPi);
}

}  // namespace smartmic
