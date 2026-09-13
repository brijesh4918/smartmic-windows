// The datagram multiplexer and session state shared by both ends.
//
// One UDP socket carries three kinds of datagram, distinguished by a single
// leading tag byte:
//
//   0x00  PAIRING   plaintext JSON. Safe in the clear because the PAKE never
//                   transmits the code and the transcript is signed (ADR-012).
//   0x01  CONTROL   sealed protocol JSON (SmartMic.Protocol)
//   0x02  MEDIA     sealed MediaHeader + Opus
//
// Nothing on tag 0x01 or 0x02 is acted on before the session is authenticated,
// and after authentication nothing is acted on that does not open under the
// session key. That is threat model T1, enforced in one place.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "smartmic/media/secure_channel.h"
#include "smartmic/media/transport.h"
#include "smartmic/protocol/codec.h"
#include "smartmic/security/pairing.h"

namespace smartmic::session {

enum class Tag : uint8_t { Pairing = 0x00, Control = 0x01, Media = 0x02 };

enum class SessionState { Idle, Pairing, Authenticated, Failed, Revoked };
std::string_view toString(SessionState s);

struct SessionCallbacks {
    std::function<void(const protocol::Message&)> onControl;
    std::function<void(const uint8_t* data, size_t len)> onMedia;
    std::function<void(SessionState)> onStateChanged;
};

// Wraps a transport with framing, encryption and the pairing handshake.
// Symmetric: the PC constructs one as Responder, the phone as Initiator.
class PeerSession {
public:
    PeerSession(const security::DeviceIdentity& self, security::PairingRole role,
                std::shared_ptr<media::ITransport> transport);

    void setCallbacks(SessionCallbacks cb) { cb_ = std::move(cb); }

    // Responder side: accept a pairing for this offer.
    void armPairing(std::string sessionId, const std::string& code,
                    security::PairingOfferBook* book);

    // Initiator side: begin pairing with a code obtained from a QR or typed in.
    bool beginPairing(const std::string& sessionId, const std::string& code);

    // Re-sends the opening pairing message. UDP has no retransmission, and the
    // very first datagram is the one most likely to be lost -- the PC may not
    // have finished binding, or the phone may have just joined the network. The
    // caller owns the timer; this class owns no clock.
    bool resendPairingHello();

    // Reconnect with an already-trusted peer (no code). Not yet used by the
    // Phase 3 binaries, which always pair; the key material is persisted so
    // this is the natural next step.
    void adoptSessionKey(const security::SessionKey& key);

    bool sendControl(const protocol::Message& m);
    bool sendMedia(const uint8_t* payload, size_t len);

    SessionState state() const { return state_; }
    const security::SessionKey& sessionKey() const { return sessionKey_; }
    std::string peerDeviceId() const;
    std::string peerFingerprint() const;
    uint64_t nextSeq() { return ++seq_; }

    // Counters worth surfacing in diagnostics.
    uint64_t rejectedDatagrams() const { return rejected_; }
    uint64_t pairingFailures() const { return pairingFailures_; }

private:
    void onDatagram(const uint8_t* data, size_t len);
    void handlePairing(const std::string& json);
    void setState(SessionState s);
    bool sendTagged(Tag tag, const uint8_t* data, size_t len);
    bool sendPairingJson(const std::string& json);
    void establish();

    security::DeviceIdentity self_;
    security::PairingRole role_;
    std::shared_ptr<media::ITransport> transport_;
    SessionCallbacks cb_;

    std::unique_ptr<security::PairingExchange> exchange_;
    security::PairingOfferBook* book_ = nullptr;
    std::string pairingSessionId_;
    std::string pairingHello_;
    std::string pendingCode_;
    security::PublicKey peerPublic_{};
    bool havePeerPublic_ = false;

    media::SecureChannel channel_;
    security::SessionKey sessionKey_{};
    SessionState state_ = SessionState::Idle;
    uint64_t seq_ = 0;
    uint64_t rejected_ = 0;
    uint64_t pairingFailures_ = 0;
    protocol::ReplayWindow<512> controlReplay_;
};

}  // namespace smartmic::session
