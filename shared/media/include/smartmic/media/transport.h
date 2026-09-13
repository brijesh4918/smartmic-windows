// The single seam between SmartMic and the network (ADR-011).
//
// Packet-oriented, asynchronous, no ordering or delivery guarantees, no
// synchronous reads -- deliberately the same contract WebRTC offers, so
// WebRtcTransport can replace SodiumUdpTransport without anything above this
// interface noticing.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smartmic::media {

struct TransportStats {
    uint64_t packetsSent = 0;
    uint64_t packetsReceived = 0;
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    uint64_t sendFailures = 0;
};

class ITransport {
public:
    using PacketHandler = std::function<void(const uint8_t* data, size_t len)>;

    virtual ~ITransport() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool send(const uint8_t* data, size_t len) = 0;
    bool send(const std::vector<uint8_t>& v) { return send(v.data(), v.size()); }
    virtual void onPacket(PacketHandler handler) = 0;
    virtual TransportStats stats() const = 0;
    virtual std::string describe() const = 0;
};

// In-process transport for tests and for the simulated phone. Two endpoints
// are cross-wired; delivery is immediate and lossless unless a ChaosTransport
// is layered on top.
class LoopbackTransport : public ITransport {
public:
    explicit LoopbackTransport(std::string name) : name_(std::move(name)) {}

    void connectTo(LoopbackTransport* peer) { peer_ = peer; }

    bool start() override { running_ = true; return true; }
    void stop() override { running_ = false; }
    bool send(const uint8_t* data, size_t len) override;
    void onPacket(PacketHandler h) override { handler_ = std::move(h); }
    TransportStats stats() const override { return stats_; }
    std::string describe() const override { return "loopback:" + name_; }

    void deliver(const uint8_t* data, size_t len);

private:
    std::string name_;
    LoopbackTransport* peer_ = nullptr;
    PacketHandler handler_;
    TransportStats stats_;
    bool running_ = false;
};

}  // namespace smartmic::media
