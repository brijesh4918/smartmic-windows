#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "smartmic/media/transport.h"

namespace smartmic::media {

// Plain UDP, one peer, blocking receive on its own thread.
//
// This is the Phase 2/3 proving vehicle from ADR-011. It carries only sealed
// packets (SecureChannel), never plaintext audio, so the "no raw audio on the
// wire" invariant holds here exactly as it will under WebRTC.
class UdpTransport : public ITransport {
public:
    UdpTransport(std::string name, uint16_t localPort);
    ~UdpTransport() override;

    // Where to send. May be set after start(), and may be updated when a peer
    // moves (a phone changing Wi-Fi, say).
    bool setPeer(const std::string& host, uint16_t port);

    // The port actually bound, which matters when localPort was 0.
    uint16_t localPort() const { return boundPort_; }

    // A listening endpoint has no peer until one contacts it, so the address of
    // the first datagram is adopted as the reply address. Note that this is
    // routing, not authentication: an unauthenticated sender can make us reply
    // to it, and everything it receives is either a pairing message (protected
    // by the PAKE) or sealed to a key it does not have. Once a peer is known,
    // the address is not changed again unless setPeer() is called, so a
    // later spoofed datagram cannot steal the conversation.
    bool hasPeer() const { return peerAddrLen_ != 0; }

    bool start() override;
    void stop() override;
    bool send(const uint8_t* data, size_t len) override;
    void onPacket(PacketHandler h) override { handler_ = std::move(h); }
    TransportStats stats() const override { return stats_; }
    std::string describe() const override;

private:
    void receiveLoop();

    std::string name_;
    uint16_t requestedPort_;
    uint16_t boundPort_ = 0;
    int socket_ = -1;
    // sockaddr_storage, kept opaque so this header does not drag in <netinet>.
    alignas(8) unsigned char peerAddr_[128]{};
    size_t peerAddrLen_ = 0;
    std::string peerDescription_;

    PacketHandler handler_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    TransportStats stats_;
};

}  // namespace smartmic::media
