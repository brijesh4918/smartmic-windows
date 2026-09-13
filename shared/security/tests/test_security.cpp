#include "smartmic/security/identity.h"
#include "smartmic/security/pairing.h"
#include "test_harness.h"

#include <string>

using namespace smartmic::security;

namespace {

struct PairResult {
    bool ok = false;
    SessionKey pcKey{};
    SessionKey phoneKey{};
};

// Runs a full honest exchange with the given codes on each side.
PairResult runPairing(const std::string& pcCode, const std::string& phoneCode,
                      const std::string& sessionId = "sid-1") {
    const auto pc = DeviceIdentity::generate();
    const auto phone = DeviceIdentity::generate();

    PairingExchange responder(pc, PairingRole::Responder, sessionId, pcCode);
    PairingExchange initiator(phone, PairingRole::Initiator, sessionId, phoneCode);

    if (responder.onPeer(initiator.publicKey(), initiator.share()) != PairingError::None) return {};
    if (initiator.onPeer(responder.publicKey(), responder.share()) != PairingError::None) return {};

    PairResult r;
    r.pcKey = responder.sessionKey();
    r.phoneKey = initiator.sessionKey();
    r.ok = responder.verifyPeerAuth(initiator.authSignature()) == PairingError::None &&
           initiator.verifyPeerAuth(responder.authSignature()) == PairingError::None;
    return r;
}

}  // namespace

SM_TEST(S1, "device identities sign and verify, and reload from their secret key") {
    CHECK(initCrypto());
    const auto id = DeviceIdentity::generate();
    const std::vector<uint8_t> msg{'h', 'e', 'l', 'l', 'o'};
    const auto sig = id.sign(msg);
    CHECK(DeviceIdentity::verify(id.publicKey(), msg, sig));

    std::vector<uint8_t> tampered = msg;
    tampered[0] = 'H';
    CHECK(!DeviceIdentity::verify(id.publicKey(), tampered, sig));

    const auto other = DeviceIdentity::generate();
    CHECK(!DeviceIdentity::verify(other.publicKey(), msg, sig));

    const std::vector<uint8_t> secret(id.secretKey().begin(), id.secretKey().end());
    const auto reloaded = DeviceIdentity::fromSecretKey(secret);
    CHECK(reloaded.has_value());
    CHECK(reloaded->deviceId() == id.deviceId());
    CHECK(DeviceIdentity::verify(reloaded->publicKey(), msg, sig));

    CHECK(!DeviceIdentity::fromSecretKey({1, 2, 3}).has_value());
}

SM_TEST(S2, "device ids round-trip and fingerprints are stable and distinct") {
    const auto a = DeviceIdentity::generate();
    const auto b = DeviceIdentity::generate();
    CHECK(a.deviceId() != b.deviceId());
    CHECK(a.fingerprint() != b.fingerprint());
    CHECK(a.fingerprint() == DeviceIdentity::fingerprintFor(a.publicKey()));

    const auto pub = DeviceIdentity::publicKeyFromDeviceId(a.deviceId());
    CHECK(pub.has_value());
    CHECK(*pub == a.publicKey());
    CHECK(!DeviceIdentity::publicKeyFromDeviceId("not-a-key").has_value());
}

SM_TEST(S3, "matching codes produce one shared session key and mutual authentication") {
    const auto r = runPairing("731482", "731482");
    CHECK(r.ok);
    CHECK(r.pcKey == r.phoneKey);
}

SM_TEST(S4, "a wrong code yields different keys and authentication fails") {
    // This is the property that makes the code meaningful: an attacker who
    // guesses wrong does not get a usable session, and learns nothing testable.
    const auto r = runPairing("731482", "731483");
    CHECK(!r.ok);
    CHECK(!(r.pcKey == r.phoneKey));
}

SM_TEST(S5, "the same code in different sessions yields unrelated keys") {
    const auto a = runPairing("111111", "111111", "sid-A");
    const auto b = runPairing("111111", "111111", "sid-B");
    CHECK(a.ok);
    CHECK(b.ok);
    CHECK(!(a.pcKey == b.pcKey));
}

SM_TEST(S6, "an invalid group element is rejected rather than processed") {
    const auto pc = DeviceIdentity::generate();
    const auto phone = DeviceIdentity::generate();
    PairingExchange responder(pc, PairingRole::Responder, "sid", "000000");

    GroupElement bad{};                       // all zeros: not a valid point
    CHECK(responder.onPeer(phone.publicKey(), bad) == PairingError::InvalidPeerElement);
    CHECK(!responder.complete());

    GroupElement garbage{};
    for (size_t i = 0; i < garbage.size(); ++i) garbage[i] = static_cast<uint8_t>(0xFF);
    CHECK(responder.onPeer(phone.publicKey(), garbage) == PairingError::InvalidPeerElement);
}

SM_TEST(S7, "a signature from the wrong device is rejected") {
    const auto pc = DeviceIdentity::generate();
    const auto phone = DeviceIdentity::generate();
    const auto attacker = DeviceIdentity::generate();

    PairingExchange responder(pc, PairingRole::Responder, "sid", "424242");
    PairingExchange initiator(phone, PairingRole::Initiator, "sid", "424242");
    CHECK(responder.onPeer(initiator.publicKey(), initiator.share()) == PairingError::None);
    CHECK(initiator.onPeer(responder.publicKey(), responder.share()) == PairingError::None);

    // Attacker signs the correct transcript with the wrong key.
    PairingExchange impostor(attacker, PairingRole::Initiator, "sid", "424242");
    CHECK(responder.verifyPeerAuth(impostor.authSignature()) == PairingError::BadSignature);
    CHECK(responder.verifyPeerAuth(initiator.authSignature()) == PairingError::None);
}

SM_TEST(S8, "pairing codes expire, throttle, and die on first use") {
    PairingPolicy policy;
    policy.ttlSeconds = 300;
    policy.maxAttempts = 5;
    PairingOfferBook book(policy);
    const auto pc = DeviceIdentity::generate();

    const uint64_t t0 = 1'000'000;
    const auto offer = book.issue(pc, "192.168.1.20", 47820, t0);
    CHECK_EQ(offer.code.size(), 6u);
    CHECK(offer.expiresAtMs == t0 + 300'000);

    CHECK(book.verify("wrong-session", offer.code, t0) == OfferStatus::Unknown);
    CHECK_EQ(book.attemptsUsed(), 0u);                 // unknown session costs no attempt

    CHECK(book.verify(offer.sessionId, offer.code, t0) == OfferStatus::Valid);
    CHECK_EQ(book.attemptsUsed(), 1u);

    // Expiry is enforced strictly at the boundary.
    CHECK(book.verify(offer.sessionId, offer.code, t0 + 300'000) == OfferStatus::Expired);

    // Single use.
    book.consume(offer.sessionId);
    CHECK(book.verify(offer.sessionId, offer.code, t0 + 1) == OfferStatus::Consumed);
}

SM_TEST(S9, "brute force runs out of attempts long before the keyspace") {
    PairingPolicy policy;
    policy.maxAttempts = 5;
    PairingOfferBook book(policy);
    const auto pc = DeviceIdentity::generate();
    const uint64_t t0 = 1'000'000;
    const auto offer = book.issue(pc, "host", 1, t0);

    int refusedBeforeThrottle = 0;
    for (int i = 0; i < 1000; ++i) {
        char guess[8];
        std::snprintf(guess, sizeof(guess), "%06d", i);
        if (std::string(guess) == offer.code) continue;
        const auto st = book.verify(offer.sessionId, guess, t0);
        if (st == OfferStatus::Throttled) break;
        ++refusedBeforeThrottle;
    }
    CHECK(refusedBeforeThrottle < 5);
    CHECK(book.verify(offer.sessionId, offer.code, t0) == OfferStatus::Throttled);
}

SM_TEST(S10, "the QR pairing URI round-trips and carries the PC fingerprint") {
    PairingOfferBook book;
    const auto pc = DeviceIdentity::generate();
    const auto offer = book.issue(pc, "192.168.1.20", 47820, 5'000'000);

    const auto parsed = parsePairingUri(offer.qrUri);
    CHECK(parsed.has_value());
    CHECK_EQ(parsed->version, 1u);
    CHECK(parsed->host == std::string("192.168.1.20"));
    CHECK_EQ(parsed->port, 47820);
    CHECK(parsed->sessionId == offer.sessionId);
    CHECK(parsed->code == offer.code);
    CHECK(parsed->pcDeviceId == pc.deviceId());
    // The fingerprint is what makes QR pairing MITM-resistant (ADR-005 / T3).
    CHECK(parsed->fingerprint == pc.fingerprint());

    CHECK(!parsePairingUri("https://example.com").has_value());
    CHECK(!parsePairingUri("smartmic://pair?v=1&host=h").has_value());
}
