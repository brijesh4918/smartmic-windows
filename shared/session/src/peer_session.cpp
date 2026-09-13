#include "smartmic/session/peer_session.h"

#include <nlohmann/json.hpp>

#include <cstring>

#include "smartmic/logging.h"

namespace smartmic::session {
namespace {
constexpr char kComponent[] = "PeerSession";
using json = nlohmann::json;

std::string b64(const uint8_t* p, size_t n) { return security::toBase64(p, n); }

template <size_t N>
bool fromB64(const json& j, const char* key, std::array<uint8_t, N>& out) {
    if (!j.contains(key) || !j.at(key).is_string()) return false;
    const auto bytes = security::fromBase64(j.at(key).get<std::string>());
    if (bytes.size() != N) return false;
    std::memcpy(out.data(), bytes.data(), N);
    return true;
}
}  // namespace

std::string_view toString(SessionState s) {
    switch (s) {
        case SessionState::Idle:          return "Idle";
        case SessionState::Pairing:       return "Pairing";
        case SessionState::Authenticated: return "Authenticated";
        case SessionState::Failed:        return "Failed";
        case SessionState::Revoked:       return "Revoked";
    }
    return "?";
}

PeerSession::PeerSession(const security::DeviceIdentity& self, security::PairingRole role,
                         std::shared_ptr<media::ITransport> transport)
    : self_(self), role_(role), transport_(std::move(transport)) {
    transport_->onPacket([this](const uint8_t* d, size_t n) { onDatagram(d, n); });
}

void PeerSession::setState(SessionState s) {
    if (state_ == s) return;
    state_ = s;
    logInfo(kComponent, "session -> " + std::string(toString(s)));
    if (cb_.onStateChanged) cb_.onStateChanged(s);
}

bool PeerSession::sendTagged(Tag tag, const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len + 1);
    out.push_back(static_cast<uint8_t>(tag));
    out.insert(out.end(), data, data + len);
    return transport_->send(out.data(), out.size());
}

bool PeerSession::sendPairingJson(const std::string& s) {
    return sendTagged(Tag::Pairing, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

void PeerSession::armPairing(std::string sessionId, const std::string& code,
                             security::PairingOfferBook* book) {
    pairingSessionId_ = std::move(sessionId);
    pendingCode_ = code;
    book_ = book;
    exchange_.reset();
    setState(SessionState::Pairing);
}

bool PeerSession::beginPairing(const std::string& sessionId, const std::string& code) {
    pairingSessionId_ = sessionId;
    exchange_ = std::make_unique<security::PairingExchange>(self_, role_, sessionId, code);
    setState(SessionState::Pairing);

    // The same PAIR1 is re-sent on retry: a fresh CPace share each time would
    // make the PC's reply ambiguous, and the share is not a nonce.
    pairingHello_ = json{{"t", "PAIR1"},
                         {"sid", sessionId},
                         {"pub", b64(self_.publicKey().data(), self_.publicKey().size())},
                         {"share", b64(exchange_->share().data(), exchange_->share().size())}}
                        .dump();
    return sendPairingJson(pairingHello_);
}

bool PeerSession::resendPairingHello() {
    if (state_ != SessionState::Pairing || pairingHello_.empty()) return false;
    return sendPairingJson(pairingHello_);
}

void PeerSession::adoptSessionKey(const security::SessionKey& key) {
    sessionKey_ = key;
    channel_.init(key, role_ == security::PairingRole::Initiator ? media::Direction::PhoneToPc
                                                                 : media::Direction::PcToPhone);
    setState(SessionState::Authenticated);
}

void PeerSession::establish() {
    sessionKey_ = exchange_->sessionKey();
    channel_.init(sessionKey_, role_ == security::PairingRole::Initiator
                                   ? media::Direction::PhoneToPc
                                   : media::Direction::PcToPhone);
    controlReplay_.reset();
    if (book_) book_->consume(pairingSessionId_);
    // The code is destroyed the moment it has done its job.
    pendingCode_.clear();
    setState(SessionState::Authenticated);
}

void PeerSession::handlePairing(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("t")) {
        ++rejected_;
        return;
    }
    const std::string t = j.value("t", "");

    if (t == "PAIR1" && role_ == security::PairingRole::Responder) {
        const std::string sid = j.value("sid", "");
        if (sid != pairingSessionId_) { ++rejected_; return; }
        if (book_) {
            const auto status = book_->check(sid, protocol::nowUnixMs());
            if (status != security::OfferStatus::Valid) {
                logWarn(kComponent, "pairing refused: " + std::string(security::toString(status)));
                sendPairingJson(json{{"t", "PAIRERR"},
                                     {"reason", std::string(security::toString(status))}}.dump());
                setState(SessionState::Failed);
                return;
            }
        }
        security::PublicKey pub{};
        security::GroupElement share{};
        if (!fromB64(j, "pub", pub) || !fromB64(j, "share", share)) { ++rejected_; return; }

        if (exchange_ && havePeerPublic_ && peerPublic_ == pub) {
            // A retried PAIR1: our PAIR2 was lost. Re-send the same share --
            // generating a fresh exchange here would silently invalidate the
            // reply the phone is still waiting for.
            sendPairingJson(json{{"t", "PAIR2"},
                                 {"pub", b64(self_.publicKey().data(), self_.publicKey().size())},
                                 {"share", b64(exchange_->share().data(), exchange_->share().size())}}
                                .dump());
            return;
        }

        exchange_ = std::make_unique<security::PairingExchange>(self_, role_, pairingSessionId_,
                                                               pendingCode_);
        if (exchange_->onPeer(pub, share) != security::PairingError::None) {
            ++pairingFailures_;
            if (book_) book_->noteFailedAttempt(pairingSessionId_);
            sendPairingJson(json{{"t", "PAIRERR"}, {"reason", "bad element"}}.dump());
            setState(SessionState::Failed);
            return;
        }
        peerPublic_ = pub;
        havePeerPublic_ = true;
        sendPairingJson(json{{"t", "PAIR2"},
                             {"pub", b64(self_.publicKey().data(), self_.publicKey().size())},
                             {"share", b64(exchange_->share().data(), exchange_->share().size())}}
                            .dump());
        return;
    }

    if (t == "PAIR2" && role_ == security::PairingRole::Initiator) {
        if (!exchange_) { ++rejected_; return; }
        security::PublicKey pub{};
        security::GroupElement share{};
        if (!fromB64(j, "pub", pub) || !fromB64(j, "share", share)) { ++rejected_; return; }
        if (exchange_->onPeer(pub, share) != security::PairingError::None) {
            ++pairingFailures_;
            setState(SessionState::Failed);
            return;
        }
        peerPublic_ = pub;
        havePeerPublic_ = true;
        const auto sig = exchange_->authSignature();
        sendPairingJson(json{{"t", "PAIR3"}, {"sig", b64(sig.data(), sig.size())}}.dump());
        return;
    }

    if (t == "PAIR3" && role_ == security::PairingRole::Responder) {
        if (!exchange_) { ++rejected_; return; }
        security::Signature sig{};
        if (!fromB64(j, "sig", sig)) { ++rejected_; return; }
        if (exchange_->verifyPeerAuth(sig) != security::PairingError::None) {
            // A wrong code lands exactly here, and this is where it costs an
            // attempt (ADR-012: the code is never transmitted, so there is
            // nothing earlier to compare).
            ++pairingFailures_;
            if (book_) book_->noteFailedAttempt(pairingSessionId_);
            logWarn(kComponent, "pairing authentication failed (wrong code or impostor)");
            sendPairingJson(json{{"t", "PAIRERR"}, {"reason", "auth failed"}}.dump());
            setState(SessionState::Failed);
            return;
        }
        const auto ours = exchange_->authSignature();
        sendPairingJson(json{{"t", "PAIR4"}, {"sig", b64(ours.data(), ours.size())}}.dump());
        establish();
        return;
    }

    if (t == "PAIR4" && role_ == security::PairingRole::Initiator) {
        if (!exchange_) { ++rejected_; return; }
        security::Signature sig{};
        if (!fromB64(j, "sig", sig)) { ++rejected_; return; }
        if (exchange_->verifyPeerAuth(sig) != security::PairingError::None) {
            ++pairingFailures_;
            setState(SessionState::Failed);
            return;
        }
        establish();
        return;
    }

    if (t == "PAIRERR") {
        ++pairingFailures_;
        logWarn(kComponent, "peer refused pairing: " + j.value("reason", "?"));
        setState(SessionState::Failed);
        return;
    }

    ++rejected_;
}

void PeerSession::onDatagram(const uint8_t* data, size_t len) {
    if (len < 1) { ++rejected_; return; }
    const auto tag = static_cast<Tag>(data[0]);
    const uint8_t* body = data + 1;
    const size_t bodyLen = len - 1;

    if (tag == Tag::Pairing) {
        handlePairing(std::string(reinterpret_cast<const char*>(body), bodyLen));
        return;
    }

    // Everything else requires an authenticated session. No exceptions, no
    // "just this one message type" -- this is the choke point for T1.
    if (state_ != SessionState::Authenticated) { ++rejected_; return; }

    std::vector<uint8_t> plain;
    if (!channel_.open(body, bodyLen, plain)) { ++rejected_; return; }

    if (tag == Tag::Media) {
        if (cb_.onMedia) cb_.onMedia(plain.data(), plain.size());
        return;
    }
    if (tag == Tag::Control) {
        const auto r = protocol::decode(plain);
        if (!r) {
            ++rejected_;
            logWarn(kComponent, "control decode failed: " + std::string(protocol::toString(r.error)));
            return;
        }
        // Sealed *and* replay-checked: the AEAD counter stops packet replay,
        // this stops an authenticated peer replaying its own old messages.
        if (!controlReplay_.accept(r.message.seq)) { ++rejected_; return; }
        if (cb_.onControl) cb_.onControl(r.message);
        return;
    }
    ++rejected_;
}

bool PeerSession::sendControl(const protocol::Message& m) {
    if (state_ != SessionState::Authenticated) return false;
    const std::string text = protocol::encode(m);
    std::vector<uint8_t> sealed;
    if (!channel_.seal(reinterpret_cast<const uint8_t*>(text.data()), text.size(), sealed)) return false;
    return sendTagged(Tag::Control, sealed.data(), sealed.size());
}

bool PeerSession::sendMedia(const uint8_t* payload, size_t len) {
    if (state_ != SessionState::Authenticated) return false;
    std::vector<uint8_t> sealed;
    if (!channel_.seal(payload, len, sealed)) return false;
    return sendTagged(Tag::Media, sealed.data(), sealed.size());
}

std::string PeerSession::peerDeviceId() const {
    return havePeerPublic_ ? security::DeviceIdentity::deviceIdFor(peerPublic_) : std::string{};
}

std::string PeerSession::peerFingerprint() const {
    return havePeerPublic_ ? security::DeviceIdentity::fingerprintFor(peerPublic_) : std::string{};
}

}  // namespace smartmic::session
