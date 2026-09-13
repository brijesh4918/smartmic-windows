#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "smartmic/audio_io.h"
#include "smartmic/ring_buffer.h"
#include "wasapi_common.h"

namespace smartmic {

// Development-only sink: renders the router's output into a third-party virtual
// cable's *input* (its render endpoint), so applications can select the cable's
// capture side and the whole product can be validated before SmartMic.Driver
// exists (ADR-001, Phase 1 of the plan).
//
// This class is exactly what SmartMicDriverLink replaces in Phase 5. Nothing
// above it changes when that happens.
class WasapiRenderSink : public IDriverLink {
public:
    WasapiRenderSink(std::string id, std::string deviceId);
    ~WasapiRenderSink() override;

    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    bool write(const float* in, size_t samples) override;

    bool endpointReady() const override { return running_.load(); }
    uint64_t underrunCount() const override { return ring_.underrunCount(); }

private:
    void renderLoop();

    std::string id_;
    std::string deviceId_;

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    WAVEFORMATEX* mixFormat_ = nullptr;
    HANDLE event_ = nullptr;
    UINT32 bufferFrames_ = 0;

    RingBuffer<float> ring_{millisToSamples(200)};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::vector<float> scratch_;
};

}  // namespace smartmic
