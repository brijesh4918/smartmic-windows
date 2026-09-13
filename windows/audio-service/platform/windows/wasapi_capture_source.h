#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "smartmic/audio_io.h"
#include "smartmic/resampler.h"
#include "smartmic/ring_buffer.h"
#include "wasapi_common.h"

namespace smartmic {

// Event-driven WASAPI shared-mode capture. Shared mode matters: the user's
// physical microphone must remain usable by other applications while SmartMic
// is running.
//
// The capture thread converts to canonical and pushes into a ring; read() pops
// from the router's thread. The two never block each other.
class WasapiCaptureSource : public IAudioSource {
public:
    WasapiCaptureSource(std::string id, std::string deviceId);
    ~WasapiCaptureSource() override;

    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    SourceStatus read(float* out, size_t samples) override;

    uint64_t droppedFrames() const { return ring_.overrunCount(); }

private:
    void captureLoop();

    std::string id_;
    std::string deviceId_;

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    WAVEFORMATEX* mixFormat_ = nullptr;
    HANDLE event_ = nullptr;

    LinearResampler resampler_;
    RingBuffer<float> ring_{millisToSamples(200)};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
    std::vector<float> scratch_;
    std::vector<float> converted_;
};

}  // namespace smartmic
