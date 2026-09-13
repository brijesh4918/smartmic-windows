// Equal-power crossfade between two sources.
//
// One position variable p in [0,1] drives both gains:
//     localGain = cos(p * pi/2)     phoneGain = sin(p * pi/2)
// so localGain^2 + phoneGain^2 == 1 for every p, by construction. That is what
// keeps perceived loudness constant through the transition instead of dipping
// in the middle the way a linear fade does.
#pragma once

#include <cstddef>
#include <cstdint>

namespace smartmic {

struct GainPair {
    float local;
    float phone;
};

class Crossfade {
public:
    // Starts fully on the local source.
    Crossfade() = default;

    // Begin moving toward `target` (0 = local, 1 = phone) over `durationMs`.
    // Calling this mid-fade retargets from the current position, so a press
    // during a release never causes a jump.
    void fadeTo(float target, uint32_t durationMs);

    // Jump with no ramp. Only for initialisation and hard error paths.
    void snapTo(float target);

    // Advance one sample and return the gains to apply to it.
    GainPair next();

    float position() const { return position_; }
    bool  active() const { return remaining_ > 0; }
    // Samples left in the current fade.
    size_t remaining() const { return remaining_; }

private:
    float  position_ = 0.0f;
    float  target_   = 0.0f;
    float  step_     = 0.0f;
    size_t remaining_ = 0;
};

// A single smoothed gain, used for gating the output to silence (Mute, error
// recovery). The demo in Phase 1 showed that gating with a boolean produces a
// ~0.4 full-scale step -- an audible click -- which is exactly the thing the
// crossfade exists to avoid, so the gate gets the same treatment.
class GainRamp {
public:
    explicit GainRamp(float initial = 1.0f) : value_(initial), target_(initial) {}

    void rampTo(float target, uint32_t durationMs);
    void snapTo(float target);

    float next();
    float value() const { return value_; }
    bool active() const { return remaining_ > 0; }

private:
    float  value_;
    float  target_;
    float  step_ = 0.0f;
    size_t remaining_ = 0;
};

}  // namespace smartmic
