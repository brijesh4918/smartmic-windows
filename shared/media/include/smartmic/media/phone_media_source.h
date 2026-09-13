// The router's phone input, as an IAudioSource.
//
// Network thread pushes encoded packets in; the audio thread pulls canonical
// frames out. This is the only place the two meet, and the critical section is
// a queue insert -- deliberately small, because the puller is the audio clock.
#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "smartmic/audio_io.h"
#include "smartmic/media/jitter_buffer.h"
#include "smartmic/media/media_packet.h"
#include "smartmic/media/opus_codec.h"

namespace smartmic::media {

class PhoneMediaSource : public IAudioSource {
public:
    PhoneMediaSource(std::string id, JitterConfig cfg = {});

    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    SourceStatus read(float* out, size_t samples) override;

    // Called from the network thread with a decrypted media payload
    // (MediaHeader + Opus). Returns false for anything malformed.
    bool onMediaPayload(const uint8_t* data, size_t len, uint64_t nowMs);

    // True once enough audio has buffered that playout will not immediately
    // starve. The service must not send PTT_READY before this (ADR-007).
    bool ready() const;

    // True when the stream has gone quiet for longer than the buffer's patience.
    bool stalled(uint64_t nowMs) const;

    void resetStream();

    JitterStats jitterStats() const;
    uint64_t malformedPackets() const { return malformed_.load(); }
    uint64_t lastPacketMs() const { return lastPacketMs_.load(); }

private:
    std::string id_;
    JitterConfig cfg_;
    mutable std::mutex mu_;
    JitterBuffer buffer_;
    OpusDecoderWrapper decoder_;
    std::atomic<bool> started_{false};
    std::atomic<uint64_t> malformed_{0};
    std::atomic<uint64_t> lastPacketMs_{0};
};

// Phone side: canonical PCM in, sealed packets out.
class MediaSender {
public:
    MediaSender() = default;

    bool open(const EncoderConfig& cfg = {});
    void close();

    // Encodes one canonical frame and produces the media payload (header +
    // Opus) ready for SecureChannel::seal.
    bool packFrame(const float* pcm, size_t samples, bool pttActive, std::vector<uint8_t>& out);

    uint32_t nextSeq() const { return seq_; }
    void resetStream();

private:
    OpusEncoderWrapper encoder_;
    uint32_t seq_ = 0;
    uint32_t timestamp_ = 0;
    bool marker_ = true;
};

}  // namespace smartmic::media
