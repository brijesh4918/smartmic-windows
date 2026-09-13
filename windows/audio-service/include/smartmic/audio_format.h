// SmartMic canonical audio format. See docs/adr/ADR-003-canonical-audio-format.md
#pragma once

#include <cstddef>
#include <cstdint>

namespace smartmic {

// The one representation the router ever sees. Sources convert up to it at
// their own boundary; sinks convert down from it at theirs.
inline constexpr uint32_t kSampleRate    = 48000;
inline constexpr uint32_t kChannels      = 1;
inline constexpr uint32_t kFrameMillis   = 20;
inline constexpr size_t   kFrameSamples  = kSampleRate / 1000 * kFrameMillis;  // 960

constexpr size_t millisToSamples(uint32_t ms) {
    return static_cast<size_t>(kSampleRate) * ms / 1000;
}

constexpr uint32_t samplesToMillis(size_t samples) {
    return static_cast<uint32_t>(samples * 1000 / kSampleRate);
}

}  // namespace smartmic
