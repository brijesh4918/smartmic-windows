#include "smartmic/security/pairing.h"

#include <sodium.h>

#include <cstdio>
#include <cstring>

namespace smartmic::security {
namespace {

constexpr char kDsiGenerator[] = "SmartMic-CPace-v1-generator";
constexpr char kDsiTranscript[] = "SmartMic-CPace-v1-transcript";
constexpr char kDsiSessionKey[] = "SmartMic-CPace-v1-sessionkey";
constexpr char kDsiAuth[] = "SmartMic-CPace-v1-auth";

void appendLengthPrefixed(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    // Every field is length-prefixed so no two different transcripts can ever
    // serialise to the same bytes.
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    out.insert(out.end(), data, data + len);
}

void appendStr(std::vector<uint8_t>& out, std::string_view s) {
    appendLengthPrefixed(out, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

}  // namespace

std::string_view toString(PairingError e) {
    switch (e) {
        case PairingError::None:               return "ok";
        case PairingError::NotReady:           return "not ready";
        case PairingError::InvalidPeerElement: return "peer sent an invalid group element";
        case PairingError::ScalarMultFailed:   return "scalar multiplication failed";
        case PairingError::BadSignature:       return "signature verification failed";
        case PairingError::WrongState:         return "wrong state";
    }
    return "?";
}

std::string_view toString(OfferStatus s) {
    switch (s) {
        case OfferStatus::Valid:     return "valid";
        case OfferStatus::Expired:   return "expired";
        case OfferStatus::Throttled: return "throttled";
        case OfferStatus::Unknown:   return "unknown session";
        case OfferStatus::Consumed:  return "already used";
    }
    return "?";
}

// --- CPace ----------------------------------------------------------------

PairingExchange::PairingExchange(const DeviceIdentity& self, PairingRole role,
                                 std::string sessionId, const std::string& code)
    : self_(self), role_(role), sessionId_(std::move(sessionId)) {
    initCrypto();
    selfPublic_ = self_.publicKey();

    // G = map_to_group( H(DSI || code || sessionId) ).
    //
    // The code is hashed into the generator rather than sent, which is the
    // whole point: recovering it from Y = y*G costs a discrete log per guess,
    // so there is no offline attack on a 20-bit secret (ADR-012).
    std::vector<uint8_t> seed;
    appendStr(seed, kDsiGenerator);
    appendStr(seed, code);
    appendStr(seed, sessionId_);

    unsigned char wide[crypto_core_ristretto255_HASHBYTES];
    crypto_generichash(wide, sizeof(wide), seed.data(), seed.size(), nullptr, 0);
    crypto_core_ristretto255_from_hash(generator_.data(), wide);
    sodium_memzero(wide, sizeof(wide));
    sodium_memzero(seed.data(), seed.size());

    crypto_core_ristretto255_scalar_random(scalar_.data());
    // from_hash never yields the identity, so this cannot fail for an honest
    // party; it is checked anyway rather than assumed.
    if (crypto_scalarmult_ristretto255(ourShare_.data(), scalar_.data(), generator_.data()) != 0) {
        sodium_memzero(ourShare_.data(), ourShare_.size());
    }
}

void PairingExchange::buildTranscript() {
    const PublicKey& initiatorPub = (role_ == PairingRole::Initiator) ? selfPublic_ : peerPublic_;
    const PublicKey& responderPub = (role_ == PairingRole::Initiator) ? peerPublic_ : selfPublic_;
    const GroupElement& initiatorY = (role_ == PairingRole::Initiator) ? ourShare_ : peerShare_;
    const GroupElement& responderY = (role_ == PairingRole::Initiator) ? peerShare_ : ourShare_;

    transcript_.clear();
    appendStr(transcript_, kDsiTranscript);
    const uint32_t version = 1;
    appendLengthPrefixed(transcript_, reinterpret_cast<const uint8_t*>(&version), sizeof(version));
    appendStr(transcript_, sessionId_);
    appendLengthPrefixed(transcript_, initiatorPub.data(), initiatorPub.size());
    appendLengthPrefixed(transcript_, responderPub.data(), responderPub.size());
    appendLengthPrefixed(transcript_, initiatorY.data(), initiatorY.size());
    appendLengthPrefixed(transcript_, responderY.data(), responderY.size());
}

PairingError PairingExchange::onPeer(const PublicKey& peerPublic, const GroupElement& peerShare) {
    if (derived_) return PairingError::WrongState;
    // Two separate checks, because they catch different things. is_valid_point
    // rejects non-canonical and off-group encodings; it does NOT reject the
    // identity, which is a perfectly valid ristretto255 point whose encoding is
    // all zeros. A peer sending the identity would force the shared secret to
    // the identity for any scalar we choose, so it is refused by name here
    // rather than being caught incidentally by scalarmult's return value.
    if (sodium_is_zero(peerShare.data(), peerShare.size()) == 1) {
        return PairingError::InvalidPeerElement;
    }
    if (crypto_core_ristretto255_is_valid_point(peerShare.data()) != 1) {
        return PairingError::InvalidPeerElement;
    }
    peerPublic_ = peerPublic;
    peerShare_ = peerShare;

    std::array<uint8_t, kGroupElementBytes> shared{};
    if (crypto_scalarmult_ristretto255(shared.data(), scalar_.data(), peerShare_.data()) != 0) {
        sodium_memzero(shared.data(), shared.size());
        return PairingError::ScalarMultFailed;
    }

    buildTranscript();

    crypto_generichash_state st;
    crypto_generichash_init(&st, reinterpret_cast<const unsigned char*>(kDsiSessionKey),
                            sizeof(kDsiSessionKey) - 1, kSessionKeyBytes);
    crypto_generichash_update(&st, shared.data(), shared.size());
    crypto_generichash_update(&st, transcript_.data(), transcript_.size());
    crypto_generichash_final(&st, sessionKey_.data(), sessionKey_.size());

    sodium_memzero(shared.data(), shared.size());
    sodium_memzero(scalar_.data(), scalar_.size());   // one-shot; no reuse
    derived_ = true;
    return PairingError::None;
}

namespace {
std::vector<uint8_t> authMessage(const std::vector<uint8_t>& transcript, const SessionKey& sk) {
    std::vector<uint8_t> m;
    appendStr(m, kDsiAuth);
    appendLengthPrefixed(m, transcript.data(), transcript.size());
    appendLengthPrefixed(m, sk.data(), sk.size());
    return m;
}
}  // namespace

Signature PairingExchange::authSignature() const {
    if (!derived_) return Signature{};
    const auto m = authMessage(transcript_, sessionKey_);
    return self_.sign(m);
}

PairingError PairingExchange::verifyPeerAuth(const Signature& sig) const {
    if (!derived_) return PairingError::NotReady;
    const auto m = authMessage(transcript_, sessionKey_);
    return DeviceIdentity::verify(peerPublic_, m, sig) ? PairingError::None
                                                       : PairingError::BadSignature;
}

// --- offers ---------------------------------------------------------------

PairingOffer PairingOfferBook::issue(const DeviceIdentity& self, const std::string& host,
                                     uint16_t port, uint64_t nowMs,
                                     std::optional<std::string> fixedCode,
                                     std::optional<std::string> fixedSessionId) {
    initCrypto();
    PairingOffer o;

    if (fixedSessionId && !fixedSessionId->empty()) {
        o.sessionId = *fixedSessionId;
    } else {
        unsigned char sid[16];
        randombytes_buf(sid, sizeof(sid));
        o.sessionId = toHex(sid, sizeof(sid));
    }

    if (fixedCode && fixedCode->size() == 6) {
        o.code = *fixedCode;
    } else {
        // Uniform over the full range: randombytes_uniform avoids the modulo
        // bias that `rand() % 1000000` would introduce.
        char digits[8];
        std::snprintf(digits, sizeof(digits), "%06u", randombytes_uniform(1000000));
        o.code = digits;
    }

    o.expiresAtMs = nowMs + static_cast<uint64_t>(policy_.ttlSeconds) * 1000ull;

    char uri[512];
    std::snprintf(uri, sizeof(uri),
                  "smartmic://pair?v=1&host=%s&port=%u&pcid=%s&fp=%s&sid=%s&code=%s&exp=%llu",
                  host.c_str(), static_cast<unsigned>(port), self.deviceId().c_str(),
                  self.fingerprint().c_str(), o.sessionId.c_str(), o.code.c_str(),
                  static_cast<unsigned long long>(o.expiresAtMs));
    o.qrUri = uri;

    offer_ = o;
    has_ = true;
    consumed_ = false;
    attempts_ = 0;
    return o;
}

bool PairingOfferBook::hasOutstanding(uint64_t nowMs) const {
    return has_ && !consumed_ && nowMs < offer_.expiresAtMs && attempts_ < policy_.maxAttempts;
}

OfferStatus PairingOfferBook::verify(const std::string& sessionId, const std::string& code,
                                     uint64_t nowMs) {
    if (!has_ || sessionId != offer_.sessionId) return OfferStatus::Unknown;
    if (consumed_) return OfferStatus::Consumed;
    if (nowMs >= offer_.expiresAtMs) return OfferStatus::Expired;
    if (attempts_ >= policy_.maxAttempts) return OfferStatus::Throttled;

    // Count the attempt before comparing, so a crash or an early return cannot
    // hand out a free guess.
    ++attempts_;

    const bool match = code.size() == offer_.code.size() &&
                       sodium_memcmp(code.data(), offer_.code.data(), offer_.code.size()) == 0;
    if (!match) {
        return attempts_ >= policy_.maxAttempts ? OfferStatus::Throttled : OfferStatus::Unknown;
    }
    return OfferStatus::Valid;
}

OfferStatus PairingOfferBook::check(const std::string& sessionId, uint64_t nowMs) const {
    if (!has_ || sessionId != offer_.sessionId) return OfferStatus::Unknown;
    if (consumed_) return OfferStatus::Consumed;
    if (nowMs >= offer_.expiresAtMs) return OfferStatus::Expired;
    if (attempts_ >= policy_.maxAttempts) return OfferStatus::Throttled;
    return OfferStatus::Valid;
}

void PairingOfferBook::noteFailedAttempt(const std::string& sessionId) {
    if (has_ && sessionId == offer_.sessionId) ++attempts_;
}

void PairingOfferBook::consume(const std::string& sessionId) {
    if (has_ && sessionId == offer_.sessionId) consumed_ = true;
}

// --- URI ------------------------------------------------------------------

std::optional<PairingUri> parsePairingUri(const std::string& uri) {
    const std::string prefix = "smartmic://pair?";
    if (uri.rfind(prefix, 0) != 0) return std::nullopt;

    PairingUri out;
    std::string rest = uri.substr(prefix.size());
    size_t pos = 0;
    while (pos <= rest.size()) {
        const size_t amp = rest.find('&', pos);
        const std::string kv = rest.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        const size_t eq = kv.find('=');
        if (eq != std::string::npos) {
            const std::string k = kv.substr(0, eq);
            const std::string v = kv.substr(eq + 1);
            if (k == "v") out.version = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
            else if (k == "host") out.host = v;
            else if (k == "port") out.port = static_cast<uint16_t>(std::strtoul(v.c_str(), nullptr, 10));
            else if (k == "pcid") out.pcDeviceId = v;
            else if (k == "fp") out.fingerprint = v;
            else if (k == "sid") out.sessionId = v;
            else if (k == "code") out.code = v;
            else if (k == "exp") out.expiresAtMs = std::strtoull(v.c_str(), nullptr, 10);
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    if (out.version != 1 || out.host.empty() || out.port == 0 || out.sessionId.empty() ||
        out.code.size() != 6 || out.pcDeviceId.empty()) {
        return std::nullopt;
    }
    return out;
}

}  // namespace smartmic::security
