// Deterministic network-fault injection, layered over any ITransport.
//
// Every reliability claim in docs/testing/11-acceptance-tests.md is only worth
// something if the faults can be reproduced exactly, so this uses a seeded PRNG
// rather than the system clock: a failing soak run can be replayed.
#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <random>
#include <vector>

#include "smartmic/media/transport.h"

namespace smartmic::media {

struct ChaosConfig {
    double lossRate = 0.0;         // 0..1, applied per packet
    double duplicateRate = 0.0;
    double reorderRate = 0.0;      // held back and released after `reorderDepth`
    uint32_t reorderDepth = 3;
    uint32_t extraLatencyMs = 0;   // recorded in the stats; delivery stays in-call
    uint32_t burstLossLength = 0;  // when a loss occurs, drop this many in a row
    uint64_t seed = 0xC0FFEE;
};

struct ChaosStats {
    uint64_t offered = 0;
    uint64_t dropped = 0;
    uint64_t duplicated = 0;
    uint64_t reordered = 0;
};

// Wraps the *outbound* side of a transport: packets handed to send() are
// subjected to the configured faults before reaching the underlying transport.
class ChaosTransport : public ITransport {
public:
    ChaosTransport(std::shared_ptr<ITransport> inner, ChaosConfig cfg);

    bool start() override { return inner_->start(); }
    void stop() override { flush(); inner_->stop(); }
    bool send(const uint8_t* data, size_t len) override;
    void onPacket(PacketHandler h) override { inner_->onPacket(std::move(h)); }
    TransportStats stats() const override { return inner_->stats(); }
    std::string describe() const override { return "chaos(" + inner_->describe() + ")"; }

    // Releases anything held back for reordering. Called on stop, and available
    // to tests that need a quiescent point.
    void flush();

    ChaosStats chaosStats() const { return stats_; }
    void setConfig(ChaosConfig cfg) { cfg_ = cfg; }

private:
    bool roll(double probability);

    std::shared_ptr<ITransport> inner_;
    ChaosConfig cfg_;
    std::mt19937_64 rng_;
    std::vector<std::vector<uint8_t>> held_;
    uint32_t burstRemaining_ = 0;
    ChaosStats stats_;
};

}  // namespace smartmic::media
