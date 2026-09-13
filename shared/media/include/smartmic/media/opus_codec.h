// Opus at the canonical format: 48 kHz, mono, 20 ms frames (ADR-003/004).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "smartmic/audio_format.h"

struct OpusEncoder;
struct OpusDecoder;

namespace smartmic::media {

struct EncoderConfig {
    int bitrateBps = 28000;     // plenty for speech at 48 k mono
    bool useInbandFec = true;   // pairs with a small packet-loss expectation
    int expectedPacketLossPct = 5;
    bool dtx = false;           // off: silence suppression would fight the router's gating
    int complexity = 7;
};

class OpusEncoderWrapper {
public:
    OpusEncoderWrapper();
    ~OpusEncoderWrapper();
    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    bool open(const EncoderConfig& cfg = {});
    void close();
    bool isOpen() const { return enc_ != nullptr; }

    // Encodes exactly one canonical frame. Returns false on failure.
    bool encode(const float* pcm, size_t samples, std::vector<uint8_t>& out);

    void setPacketLossPercent(int pct);

private:
    OpusEncoder* enc_ = nullptr;
    EncoderConfig cfg_;
};

class OpusDecoderWrapper {
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();
    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    bool open();
    void close();
    bool isOpen() const { return dec_ != nullptr; }

    // Decodes one packet into exactly kFrameSamples samples.
    bool decode(const uint8_t* data, size_t len, float* pcm, size_t samples);

    // Packet-loss concealment: Opus synthesises a frame from its own state,
    // which sounds far better than substituting silence for a dropped packet.
    bool conceal(float* pcm, size_t samples);

private:
    OpusDecoder* dec_ = nullptr;
};

}  // namespace smartmic::media
