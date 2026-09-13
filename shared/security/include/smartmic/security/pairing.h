// Pairing: CPace over ristretto255, then a mutually verified transcript
// signature. See docs/adr/ADR-012-pairing-key-exchange.md.
//
// RELEASE GATE: this file is protocol code built on reviewed primitives, not a
// reviewed protocol implementation. An external cryptographic review is a
// release-blocking item (Phase 7).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "smartmic/security/identity.h"

namespace smartmic::security {

inline constexpr size_t kGroupElementBytes = 32;
inline constexpr size_t kSessionKeyBytes = 32;

using GroupElement = std::array<uint8_t, kGroupElementBytes>;
using SessionKey = std::array<uint8_t, kSessionKeyBytes>;

// Role decides transcript ordering only; the protocol is balanced.
enum class PairingRole { Initiator, Responder };

enum class PairingError {
    None,
    NotReady,
    InvalidPeerElement,   // not a valid ristretto255 point, or the identity
    ScalarMultFailed,
    BadSignature,
    WrongState,
};
std::string_view toString(PairingError e);

// One side of a pairing exchange.
//
//   Initiator                       Responder
//     message1()  --- Y_i, pub_i --->
//                 <-- Y_r, pub_r ---  message1()
//     onPeer(...)                     onPeer(...)
//     authSignature() --- sig_i --->
//                 <-- sig_r ------- authSignature()
//     verifyPeerAuth(sig_r)           verifyPeerAuth(sig_i)
//     sessionKey()                    sessionKey()
class PairingExchange {
public:
    PairingExchange(const DeviceIdentity& self, PairingRole role,
                    std::string sessionId, const std::string& code);

    // Our CPace share. Safe to send in the clear.
    const GroupElement& share() const { return ourShare_; }
    const PublicKey& publicKey() const { return selfPublic_; }

    // Consume the peer's share and long-term public key, deriving the session
    // key. Fails closed on any invalid element.
    PairingError onPeer(const PublicKey& peerPublic, const GroupElement& peerShare);

    bool complete() const { return derived_; }
    const SessionKey& sessionKey() const { return sessionKey_; }

    Signature authSignature() const;
    PairingError verifyPeerAuth(const Signature& sig) const;

    // Exposed for tests and for binding the media layer to this exchange.
    const std::vector<uint8_t>& transcript() const { return transcript_; }

private:
    void buildTranscript();

    DeviceIdentity self_;
    PairingRole role_;
    std::string sessionId_;
    PublicKey selfPublic_{};
    PublicKey peerPublic_{};
    GroupElement generator_{};
    GroupElement ourShare_{};
    GroupElement peerShare_{};
    std::array<uint8_t, 32> scalar_{};
    SessionKey sessionKey_{};
    std::vector<uint8_t> transcript_;
    bool derived_ = false;
};

// --- code issuance and policy ---------------------------------------------

struct PairingPolicy {
    uint32_t ttlSeconds = 300;
    uint32_t maxAttempts = 5;
    // Exactly one pairing may be in flight. A second offer replaces the first,
    // which is what "generate a new code" means in the UI.
    bool singleOutstandingOffer = true;
};

struct PairingOffer {
    std::string sessionId;
    std::string code;          // six digits, uniform over 000000..999999
    uint64_t expiresAtMs = 0;
    std::string qrUri;
};

enum class OfferStatus { Valid, Expired, Throttled, Unknown, Consumed };
std::string_view toString(OfferStatus s);

// PC-side issuer. Owns TTL, attempt limiting and single-use semantics --
// the properties that make a 20-bit secret safe (threat model T2).
class PairingOfferBook {
public:
    explicit PairingOfferBook(PairingPolicy policy = {}) : policy_(policy) {}

    // `fixedCode` / `fixedSessionId` exist so headless tests and the scripted
    // demo can pair without a human reading a screen. The desktop UI never
    // passes them; a fixed code is a test fixture, not a product feature.
    PairingOffer issue(const DeviceIdentity& self, const std::string& host, uint16_t port,
                       uint64_t nowMs,
                       std::optional<std::string> fixedCode = std::nullopt,
                       std::optional<std::string> fixedSessionId = std::nullopt);

    // Checks a presented code against the outstanding offer. Every call that
    // reaches the code comparison counts as an attempt, whether or not it
    // matches, so an attacker cannot get free guesses by malforming requests.
    OfferStatus verify(const std::string& sessionId, const std::string& code, uint64_t nowMs);

    // The PAKE path never sends the code, so there is nothing to compare:
    // the offer's liveness is checked here, and a wrong code shows up later as
    // a failed transcript signature. Attempts are counted on that failure.
    OfferStatus check(const std::string& sessionId, uint64_t nowMs) const;
    void noteFailedAttempt(const std::string& sessionId);

    // Called once the exchange has actually succeeded; the code dies here.
    void consume(const std::string& sessionId);

    void clear() { has_ = false; }
    bool hasOutstanding(uint64_t nowMs) const;
    uint32_t attemptsUsed() const { return attempts_; }
    const PairingOffer& outstanding() const { return offer_; }

private:
    PairingPolicy policy_;
    PairingOffer offer_;
    bool has_ = false;
    bool consumed_ = false;
    uint32_t attempts_ = 0;
};

// Parses smartmic://pair?... from a scanned QR code.
struct PairingUri {
    uint32_t version = 0;
    std::string host;
    uint16_t port = 0;
    std::string pcDeviceId;
    std::string fingerprint;
    std::string sessionId;
    std::string code;
    uint64_t expiresAtMs = 0;
};
std::optional<PairingUri> parsePairingUri(const std::string& uri);

}  // namespace smartmic::security
