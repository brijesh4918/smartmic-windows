// Adaptive jitter buffer.
//
// Holds *encoded* packets and decodes on pop, so Opus's own packet-loss
// concealment can run with correct decoder state -- concealment done by
// substituting silence at the PCM layer sounds far worse.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "smartmic/audio_format.h"
#include "smartmic/media/opus_codec.h"

namespace smartmic::media {

struct JitterConfig {
    uint32_t targetMs = 40;      // starting target depth
    uint32_t minMs = 20;
    uint32_t maxMs = 200;
    uint32_t prefillMs = 40;     // must be met before the router is told "ready"
    // How long a gap in arrivals before the buffer declares the stream dead.
    // The router's own watchdog is the real safety net (ADR-008); this is just
    // the buffer refusing to pretend.
    uint32_t stallMs = 300;
    bool adaptive = true;
};

struct JitterStats {
    uint64_t pushed = 0;
    uint64_t popped = 0;
    uint64_t concealed = 0;      // frames produced by PLC
    uint64_t duplicates = 0;
    uint64_t reordered = 0;
    uint64_t tooLate = 0;        // arrived after its slot had already been served
    uint64_t overflow = 0;       // dropped because the buffer was full
    uint32_t currentDepthMs = 0;
    uint32_t targetMs = 0;
};

class JitterBuffer {
public:
    explicit JitterBuffer(JitterConfig cfg = {});

    void reset();

    // Insert a received packet. `arrivalMs` is a monotonic clock reading.
    void push(uint32_t seq, const uint8_t* payload, size_t len, uint64_t arrivalMs);

    // True once enough audio has accumulated that playout can start without
    // immediately starving. The router must not enter PHONE_ACTIVE before this.
    bool ready() const;

    // True when nothing has arrived for longer than stallMs.
    bool stalled(uint64_t nowMs) const;

    // Produce exactly one canonical frame. Returns true if it came from a real
    // packet, false if it was concealed or silence.
    bool pop(OpusDecoderWrapper& decoder, float* out, size_t samples, uint64_t nowMs);

    JitterStats stats() const;
    uint32_t depthMs() const;

private:
    void adapt(uint64_t arrivalMs);

    struct Entry {
        std::vector<uint8_t> payload;
        uint64_t arrivalMs = 0;
    };

    JitterConfig cfg_;
    std::map<uint32_t, Entry> queue_;   // ordered by seq; reordering is free
    bool started_ = false;
    uint32_t nextSeq_ = 0;
    uint64_t lastArrivalMs_ = 0;
    uint32_t targetMs_ = 0;

    // Arrival-jitter estimate, RFC 3550 style: a smoothed mean deviation of the
    // inter-arrival interval from the nominal frame interval.
    double jitterEstimate_ = 0.0;
    uint64_t prevArrivalMs_ = 0;
    bool havePrevArrival_ = false;

    JitterStats stats_;
};

}  // namespace smartmic::media
