#include "smartmic/media/jitter_buffer.h"

#include <algorithm>
#include <cmath>

namespace smartmic::media {
namespace {
constexpr uint32_t kFrameMs = kFrameMillis;
size_t maxEntries(const JitterConfig& c) { return (c.maxMs / kFrameMs) + 4; }
}

JitterBuffer::JitterBuffer(JitterConfig cfg) : cfg_(cfg), targetMs_(cfg.targetMs) {}

void JitterBuffer::reset() {
    queue_.clear();
    started_ = false;
    nextSeq_ = 0;
    lastArrivalMs_ = 0;
    targetMs_ = cfg_.targetMs;
    jitterEstimate_ = 0.0;
    havePrevArrival_ = false;
    stats_ = JitterStats{};
}

void JitterBuffer::adapt(uint64_t arrivalMs) {
    if (havePrevArrival_) {
        const double delta = std::fabs(static_cast<double>(arrivalMs - prevArrivalMs_) -
                                       static_cast<double>(kFrameMs));
        // Same smoothing factor as RTP's interarrival jitter: slow enough not to
        // chase a single late packet, fast enough to follow a real change.
        jitterEstimate_ += (delta - jitterEstimate_) / 16.0;
    }
    prevArrivalMs_ = arrivalMs;
    havePrevArrival_ = true;

    if (!cfg_.adaptive) return;
    // Target three standard-deviations-ish of observed jitter, clamped. Grows
    // quickly when the network gets worse, shrinks slowly so a brief calm spell
    // does not cause a fresh round of dropouts.
    const auto want = static_cast<uint32_t>(std::ceil(jitterEstimate_ * 3.0)) + cfg_.minMs;
    const uint32_t clamped = std::clamp(want, cfg_.minMs, cfg_.maxMs);
    if (clamped > targetMs_) targetMs_ = clamped;
    else if (clamped + kFrameMs < targetMs_) targetMs_ -= kFrameMs;
}

void JitterBuffer::push(uint32_t seq, const uint8_t* payload, size_t len, uint64_t arrivalMs) {
    lastArrivalMs_ = arrivalMs;
    ++stats_.pushed;
    adapt(arrivalMs);

    if (!started_) {
        started_ = true;
        nextSeq_ = seq;
    }

    // Signed comparison handles sequence wrap correctly.
    const auto age = static_cast<int32_t>(seq - nextSeq_);
    if (age < 0) {
        ++stats_.tooLate;   // its slot has already been played out
        return;
    }
    if (queue_.count(seq)) {
        ++stats_.duplicates;
        return;
    }
    if (!queue_.empty() && seq < queue_.rbegin()->first) ++stats_.reordered;

    if (queue_.size() >= maxEntries(cfg_)) {
        // Full: drop the oldest rather than the newest. Late audio is worse
        // than missing audio -- the same rule as the driver ring (ADR-002).
        queue_.erase(queue_.begin());
        ++stats_.overflow;
    }

    Entry e;
    e.payload.assign(payload, payload + len);
    e.arrivalMs = arrivalMs;
    queue_.emplace(seq, std::move(e));
}

bool JitterBuffer::ready() const {
    return started_ && depthMs() >= cfg_.prefillMs;
}

bool JitterBuffer::stalled(uint64_t nowMs) const {
    if (!started_) return false;
    return nowMs > lastArrivalMs_ && (nowMs - lastArrivalMs_) > cfg_.stallMs;
}

uint32_t JitterBuffer::depthMs() const {
    return static_cast<uint32_t>(queue_.size() * kFrameMs);
}

bool JitterBuffer::pop(OpusDecoderWrapper& decoder, float* out, size_t samples, uint64_t nowMs) {
    (void)nowMs;
    if (!started_ || samples != kFrameSamples) {
        std::fill(out, out + samples, 0.0f);
        return false;
    }

    const auto it = queue_.find(nextSeq_);
    if (it == queue_.end()) {
        // The frame we wanted is not here. If later frames are waiting and the
        // buffer is already deeper than target, the packet is genuinely lost
        // and we skip it; otherwise it may still be in flight, so conceal and
        // hold the slot open for one more frame.
        const bool overdue = !queue_.empty() && depthMs() > targetMs_;
        ++stats_.concealed;
        decoder.conceal(out, samples);
        if (overdue || !queue_.empty()) ++nextSeq_;
        return false;
    }

    const bool ok = decoder.decode(it->second.payload.data(), it->second.payload.size(), out, samples);
    queue_.erase(it);
    ++nextSeq_;
    ++stats_.popped;
    return ok;
}

JitterStats JitterBuffer::stats() const {
    JitterStats s = stats_;
    s.currentDepthMs = depthMs();
    s.targetMs = targetMs_;
    return s;
}

}  // namespace smartmic::media
