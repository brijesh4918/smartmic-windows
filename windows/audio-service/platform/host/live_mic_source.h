// The host machine's real microphone, as an IAudioSource.
//
// On macOS this lets `smartmic-service` run a genuinely representative test:
// the Mac's own microphone stands in for "the PC microphone", the iPhone
// stands in for the phone, and the recorded WAV shows the router switching
// between two real voices. Without it the local source is a tone, which proves
// the plumbing but not much else.
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "capture.h"
#include "smartmic/audio_io.h"
#include "smartmic/ring_buffer.h"

namespace smartmic::host {

class LiveMicSource : public IAudioSource {
public:
    explicit LiveMicSource(std::string id);
    ~LiveMicSource() override;

    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    SourceStatus read(float* out, size_t samples) override;

    bool live() const { return live_.load(); }
    const char* backend() const;

private:
    std::string id_;
    std::unique_ptr<phone::ICapture> capture_;
    // The capture callback runs on the platform's audio thread; the router
    // pulls from its own. Same single-producer/single-consumer contract as the
    // driver ring, and the same policy: never block, drop oldest on overrun.
    RingBuffer<float> ring_{millisToSamples(400)};
    std::atomic<bool> live_{false};
};

}  // namespace smartmic::host
