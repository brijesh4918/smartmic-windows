#include "smartmic/media/secure_channel.h"

#include <sodium.h>

#include <cstring>

namespace smartmic::media {
namespace {

// crypto_kdf requires exactly 8 bytes of context.
constexpr char kContext[9] = "SMARTMIC";

constexpr uint64_t kKeyPhoneToPc = 1;
constexpr uint64_t kKeyPcToPhone = 2;
constexpr uint64_t kNoncePhoneToPc = 11;
constexpr uint64_t kNoncePcToPhone = 12;

constexpr size_t kNoncePrefixBytes = crypto_secretbox_NONCEBYTES - 8;  // 16

void derive(std::vector<uint8_t>& out, size_t len, uint64_t id,
            const security::SessionKey& key) {
    out.resize(len);
    crypto_kdf_derive_from_key(out.data(), len, id, kContext, key.data());
}

void buildNonce(uint8_t* nonce, const std::vector<uint8_t>& prefix, uint64_t counter) {
    std::memcpy(nonce, prefix.data(), kNoncePrefixBytes);
    for (int i = 0; i < 8; ++i) {
        nonce[kNoncePrefixBytes + i] = static_cast<uint8_t>((counter >> (8 * i)) & 0xFF);
    }
}

}  // namespace

bool SecureChannel::init(const security::SessionKey& sessionKey, Direction send) {
    static_assert(crypto_kdf_KEYBYTES == security::kSessionKeyBytes,
                  "session key size must match crypto_kdf key size");
    if (sodium_init() < 0) return false;

    const uint64_t sendKeyId = (send == Direction::PhoneToPc) ? kKeyPhoneToPc : kKeyPcToPhone;
    const uint64_t recvKeyId = (send == Direction::PhoneToPc) ? kKeyPcToPhone : kKeyPhoneToPc;
    const uint64_t sendNonceId = (send == Direction::PhoneToPc) ? kNoncePhoneToPc : kNoncePcToPhone;
    const uint64_t recvNonceId = (send == Direction::PhoneToPc) ? kNoncePcToPhone : kNoncePhoneToPc;

    derive(sendKey_, crypto_secretbox_KEYBYTES, sendKeyId, sessionKey);
    derive(recvKey_, crypto_secretbox_KEYBYTES, recvKeyId, sessionKey);
    derive(sendNoncePrefix_, kNoncePrefixBytes, sendNonceId, sessionKey);
    derive(recvNoncePrefix_, kNoncePrefixBytes, recvNonceId, sessionKey);

    counter_ = 0;
    replay_.reset();
    initialised_ = true;
    return true;
}

bool SecureChannel::seal(const uint8_t* plain, size_t len, std::vector<uint8_t>& out) {
    if (!initialised_) return false;

    const uint64_t counter = counter_++;
    uint8_t nonce[crypto_secretbox_NONCEBYTES];
    buildNonce(nonce, sendNoncePrefix_, counter);

    out.resize(8 + crypto_secretbox_MACBYTES + len);
    for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>((counter >> (8 * i)) & 0xFF);

    if (crypto_secretbox_easy(out.data() + 8, plain, len, nonce, sendKey_.data()) != 0) {
        out.clear();
        return false;
    }
    ++sealed_;
    return true;
}

bool SecureChannel::open(const uint8_t* packet, size_t len, std::vector<uint8_t>& out) {
    if (!initialised_) return false;
    if (len < kSecureOverhead) {
        ++malformed_;
        return false;
    }

    uint64_t counter = 0;
    for (int i = 0; i < 8; ++i) counter |= static_cast<uint64_t>(packet[i]) << (8 * i);

    uint8_t nonce[crypto_secretbox_NONCEBYTES];
    buildNonce(nonce, recvNoncePrefix_, counter);

    const size_t cipherLen = len - 8;
    out.resize(cipherLen - crypto_secretbox_MACBYTES);
    if (crypto_secretbox_open_easy(out.data(), packet + 8, cipherLen, nonce, recvKey_.data()) != 0) {
        ++authFailures_;
        out.clear();
        return false;
    }

    // Authenticate first, then replay-check: an attacker must not be able to
    // poison the replay window with forged counters.
    if (!replay_.accept(counter)) {
        ++replayRejects_;
        out.clear();
        return false;
    }
    ++opened_;
    return true;
}

}  // namespace smartmic::media
