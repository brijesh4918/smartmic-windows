// Microphone capture behind one interface (ADR-009's AudioCaptureEngine).
//
// AAudio on Android, AudioUnit on iOS/macOS, a tone generator everywhere else
// so the engine can be exercised without a microphone.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>

namespace smartmic::phone {

class ICapture {
public:
    // Called from the platform's audio thread with canonical-rate mono float.
    using FrameHandler = std::function<void(const float* pcm, size_t samples)>;

    virtual ~ICapture() = default;
    virtual bool start(FrameHandler handler) = 0;
    virtual void stop() = 0;
    virtual const char* name() const = 0;
};

std::unique_ptr<ICapture> createPlatformCapture();

}  // namespace smartmic::phone
