#include "smartmic/media/chaos_transport.h"

#include <algorithm>

namespace smartmic::media {

ChaosTransport::ChaosTransport(std::shared_ptr<ITransport> inner, ChaosConfig cfg)
    : inner_(std::move(inner)), cfg_(cfg), rng_(cfg.seed) {}

bool ChaosTransport::roll(double probability) {
    if (probability <= 0.0) return false;
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng_) < probability;
}

bool ChaosTransport::send(const uint8_t* data, size_t len) {
    ++stats_.offered;

    // Burst loss: real Wi-Fi drops runs of packets, not independent samples,
    // and a jitter buffer that survives 5 % uniform loss can still fall over on
    // a 60 ms burst.
    if (burstRemaining_ > 0) {
        --burstRemaining_;
        ++stats_.dropped;
        return true;   // the sender cannot tell, which is the point
    }
    if (roll(cfg_.lossRate)) {
        ++stats_.dropped;
        if (cfg_.burstLossLength > 1) burstRemaining_ = cfg_.burstLossLength - 1;
        return true;
    }

    std::vector<uint8_t> packet(data, data + len);

    if (roll(cfg_.reorderRate)) {
        held_.push_back(std::move(packet));
        ++stats_.reordered;
        if (held_.size() >= cfg_.reorderDepth) {
            // Release in reverse, which is the worst case for a naive receiver.
            for (auto it = held_.rbegin(); it != held_.rend(); ++it) {
                inner_->send(it->data(), it->size());
            }
            held_.clear();
        }
        return true;
    }

    const bool ok = inner_->send(packet.data(), packet.size());
    if (roll(cfg_.duplicateRate)) {
        inner_->send(packet.data(), packet.size());
        ++stats_.duplicated;
    }
    return ok;
}

void ChaosTransport::flush() {
    for (auto& p : held_) inner_->send(p.data(), p.size());
    held_.clear();
}

}  // namespace smartmic::media
