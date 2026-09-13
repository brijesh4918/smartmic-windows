// AEAD framing for the media path (ADR-011).
//
// One session key, two directions, two independently derived subkeys and nonce
// prefixes, so the two directions can never collide on a nonce. The counter is
// explicit in the packet and replay-checked on receipt.
#pragma once

#include <cstdint>
#include <vector>

#include "smartmic/protocol/replay_window.h"
#include "smartmic/security/pairing.h"

namespace smartmic::media {

enum class Direction { PhoneToPc, PcToPhone };

// Wire overhead: 8-byte counter + 16-byte Poly1305 tag.
inline constexpr size_t kSecureOverhead = 8 + 16;

class SecureChannel {
public:
    // `send` is the direction this endpoint transmits in; the receive direction
    // is the other one.
    bool init(const security::SessionKey& sessionKey, Direction send);

    bool seal(const uint8_t* plain, size_t len, std::vector<uint8_t>& out);

    // Returns false for a forged, corrupt, truncated or replayed packet. The
    // caller cannot tell which, and does not need to.
    bool open(const uint8_t* packet, size_t len, std::vector<uint8_t>& out);

    uint64_t sealed() const { return sealed_; }
    uint64_t opened() const { return opened_; }
    uint64_t authFailures() const { return authFailures_; }
    uint64_t replayRejects() const { return replayRejects_; }
    // Packets too short to contain a counter and a tag. Counted separately
    // from authentication failures: a runaway malformed count means something
    // is wrong with the transport, a runaway auth count means something is
    // wrong with the peer.
    uint64_t malformed() const { return malformed_; }
    uint64_t rejected() const { return authFailures_ + replayRejects_ + malformed_; }

private:
    bool initialised_ = false;
    std::vector<uint8_t> sendKey_, recvKey_;
    std::vector<uint8_t> sendNoncePrefix_, recvNoncePrefix_;
    uint64_t counter_ = 0;
    protocol::ReplayWindow<1024> replay_;
    uint64_t sealed_ = 0, opened_ = 0, authFailures_ = 0, replayRejects_ = 0, malformed_ = 0;
};

}  // namespace smartmic::media
