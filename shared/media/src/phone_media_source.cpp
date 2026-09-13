#include "smartmic/media/phone_media_source.h"

#include <algorithm>

#include "smartmic/logging.h"

namespace smartmic::media {
namespace {
constexpr char kComponent[] = "PhoneMedia";
}

PhoneMediaSource::PhoneMediaSource(std::string id, JitterConfig cfg)
    : id_(std::move(id)), cfg_(cfg), buffer_(cfg) {}

bool PhoneMediaSource::start() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!decoder_.open()) return false;
    buffer_.reset();
    started_.store(true);
    return true;
}

void PhoneMediaSource::stop() {
    std::lock_guard<std::mutex> lk(mu_);
    started_.store(false);
    buffer_.reset();
    decoder_.close();
}

void PhoneMediaSource::resetStream() {
    std::lock_guard<std::mutex> lk(mu_);
    buffer_.reset();
}

bool PhoneMediaSource::onMediaPayload(const uint8_t* data, size_t len, uint64_t nowMs) {
    MediaHeader h{};
    if (!readHeader(data, len, h)) {
        malformed_.fetch_add(1);
        return false;
    }
    const size_t payloadLen = len - sizeof(MediaHeader);
    if (payloadLen == 0) {
        malformed_.fetch_add(1);
        return false;
    }
    lastPacketMs_.store(nowMs);

    std::lock_guard<std::mutex> lk(mu_);
    if (h.flags & kFlagMarker) {
        // A new talk spurt: the previous stream's sequence space is gone.
        buffer_.reset();
    }
    buffer_.push(h.seq, data + sizeof(MediaHeader), payloadLen, nowMs);
    return true;
}

bool PhoneMediaSource::ready() const {
    std::lock_guard<std::mutex> lk(mu_);
    return started_.load() && buffer_.ready();
}

bool PhoneMediaSource::stalled(uint64_t nowMs) const {
    std::lock_guard<std::mutex> lk(mu_);
    return buffer_.stalled(nowMs);
}

SourceStatus PhoneMediaSource::read(float* out, size_t samples) {
    if (!started_.load()) {
        std::fill(out, out + samples, 0.0f);
        return SourceStatus::Failed;
    }
    std::lock_guard<std::mutex> lk(mu_);
    const bool real = buffer_.pop(decoder_, out, samples, 0);
    return real ? SourceStatus::Ok : SourceStatus::Underrun;
}

JitterStats PhoneMediaSource::jitterStats() const {
    std::lock_guard<std::mutex> lk(mu_);
    return buffer_.stats();
}

// --- sender ---------------------------------------------------------------

bool MediaSender::open(const EncoderConfig& cfg) {
    seq_ = 0;
    timestamp_ = 0;
    marker_ = true;
    return encoder_.open(cfg);
}

void MediaSender::close() { encoder_.close(); }

void MediaSender::resetStream() { marker_ = true; }

bool MediaSender::packFrame(const float* pcm, size_t samples, bool pttActive,
                            std::vector<uint8_t>& out) {
    std::vector<uint8_t> opus;
    if (!encoder_.encode(pcm, samples, opus)) return false;

    MediaHeader h{};
    h.version = kMediaVersion;
    h.flags = static_cast<uint8_t>((pttActive ? kFlagPttActive : kFlagNone) |
                                   (marker_ ? kFlagMarker : kFlagNone));
    h.reserved = 0;
    h.seq = seq_++;
    h.timestampSamples = timestamp_;
    timestamp_ += static_cast<uint32_t>(samples);
    marker_ = false;

    out.clear();
    out.reserve(sizeof(MediaHeader) + opus.size());
    writeHeader(h, out);
    out.insert(out.end(), opus.begin(), opus.end());
    return true;
}

}  // namespace smartmic::media
