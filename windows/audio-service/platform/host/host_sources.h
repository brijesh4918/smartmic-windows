// Portable sources and sinks. These exist so the router can be developed,
// tested and *listened to* on any machine -- including CI and a non-Windows
// workstation -- before a single line of WASAPI or driver code runs.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "smartmic/audio_io.h"
#include "smartmic/resampler.h"

namespace smartmic::host {

// A steady sine. Phase is continuous across reads, so any discontinuity in the
// router's output is the router's fault and nothing else's.
class ToneSource : public IAudioSource {
public:
    ToneSource(std::string id, float hz, float amplitude = 0.5f);
    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    SourceStatus read(float* out, size_t samples) override;

    void setFrequency(float hz) { hz_ = hz; }
    // Starting phase in radians. Tests use this to make sure a switch is never
    // accidentally aligned to a zero crossing of both tones at once.
    void setPhase(double radians) { phase_ = radians; }
    // Makes the source report Failed from the next read onward (test A13).
    void failFromNow() { failed_ = true; }

private:
    std::string id_;
    float hz_;
    float amp_;
    double phase_ = 0.0;
    bool started_ = false;
    bool failed_ = false;
};

class SilenceSource : public IAudioSource {
public:
    explicit SilenceSource(std::string id) : id_(std::move(id)) {}
    const std::string& id() const override { return id_; }
    bool start() override { return true; }
    void stop() override {}
    SourceStatus read(float* out, size_t samples) override;

private:
    std::string id_;
};

// 16-bit PCM WAV reader; resamples and downmixes to canonical on the fly.
class WavFileSource : public IAudioSource {
public:
    WavFileSource(std::string id, std::string path, bool loop = true);
    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    SourceStatus read(float* out, size_t samples) override;

private:
    bool refill(size_t needed);

    std::string id_;
    std::string path_;
    bool loop_;
    std::FILE* fp_ = nullptr;
    long dataStart_ = 0;
    long dataBytes_ = 0;
    long consumed_ = 0;
    uint32_t rate_ = kSampleRate;
    uint16_t channels_ = 1;
    LinearResampler resampler_;
    std::vector<float> pending_;
    size_t pendingPos_ = 0;
};

// 16-bit PCM WAV writer. The audible Phase 1 artifact.
class WavFileSink : public IAudioSink {
public:
    WavFileSink(std::string id, std::string path);
    ~WavFileSink() override;
    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    bool write(const float* in, size_t samples) override;

private:
    void finalise();

    std::string id_;
    std::string path_;
    std::FILE* fp_ = nullptr;
    uint32_t framesWritten_ = 0;
};

// Captures everything written, for assertions in tests.
class MemorySink : public IAudioSink {
public:
    explicit MemorySink(std::string id) : id_(std::move(id)) {}
    const std::string& id() const override { return id_; }
    bool start() override { return true; }
    void stop() override {}
    bool write(const float* in, size_t samples) override;

    const std::vector<float>& samples() const { return samples_; }
    size_t writeCount() const { return writes_; }
    void failFromNow() { failing_ = true; }

private:
    std::string id_;
    std::vector<float> samples_;
    size_t writes_ = 0;
    bool failing_ = false;
};

}  // namespace smartmic::host
