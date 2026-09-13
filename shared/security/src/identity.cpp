#include "smartmic/security/identity.h"

#include <sodium.h>

#include <cstring>

namespace smartmic::security {

bool initCrypto() {
    static const int rc = sodium_init();
    return rc >= 0;
}

std::string toBase64(const uint8_t* data, size_t len) {
    const size_t max = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(max, '\0');
    sodium_bin2base64(out.data(), max, data, len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::vector<uint8_t> fromBase64(const std::string& s) {
    std::vector<uint8_t> out(s.size());
    size_t len = 0;
    if (sodium_base642bin(out.data(), out.size(), s.data(), s.size(), nullptr, &len, nullptr,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
        return {};
    }
    out.resize(len);
    return out;
}

std::string toHex(const uint8_t* data, size_t len) {
    std::string out(len * 2 + 1, '\0');
    sodium_bin2hex(out.data(), out.size(), data, len);
    out.resize(len * 2);
    return out;
}

DeviceIdentity DeviceIdentity::generate() {
    initCrypto();
    DeviceIdentity id;
    crypto_sign_keypair(id.public_.data(), id.secret_.data());
    return id;
}

std::optional<DeviceIdentity> DeviceIdentity::fromSecretKey(const std::vector<uint8_t>& secret) {
    initCrypto();
    if (secret.size() != kSecretKeyBytes) return std::nullopt;
    DeviceIdentity id;
    std::memcpy(id.secret_.data(), secret.data(), kSecretKeyBytes);
    if (crypto_sign_ed25519_sk_to_pk(id.public_.data(), id.secret_.data()) != 0) return std::nullopt;
    return id;
}

std::string DeviceIdentity::deviceIdFor(const PublicKey& pub) {
    return toBase64(pub.data(), pub.size());
}

std::string DeviceIdentity::fingerprintFor(const PublicKey& pub) {
    // Short, comparable by a human across two screens. Truncation is fine here:
    // the fingerprint is a MITM check for an interactive user, backed by the
    // full-key signature check that follows it.
    unsigned char h[crypto_generichash_BYTES];
    crypto_generichash(h, sizeof(h), pub.data(), pub.size(),
                       reinterpret_cast<const unsigned char*>("SmartMic-fp-v1"), 14);
    const std::string hex = toHex(h, 8);
    std::string grouped;
    for (size_t i = 0; i < hex.size(); i += 4) {
        if (i) grouped += '-';
        grouped += hex.substr(i, 4);
    }
    return grouped;
}

std::optional<PublicKey> DeviceIdentity::publicKeyFromDeviceId(const std::string& id) {
    const auto bytes = fromBase64(id);
    if (bytes.size() != kPublicKeyBytes) return std::nullopt;
    PublicKey pub{};
    std::memcpy(pub.data(), bytes.data(), kPublicKeyBytes);
    return pub;
}

std::string DeviceIdentity::deviceId() const { return deviceIdFor(public_); }
std::string DeviceIdentity::fingerprint() const { return fingerprintFor(public_); }

Signature DeviceIdentity::sign(const uint8_t* data, size_t len) const {
    Signature sig{};
    unsigned long long sigLen = 0;
    crypto_sign_detached(sig.data(), &sigLen, data, len, secret_.data());
    return sig;
}

bool DeviceIdentity::verify(const PublicKey& pub, const uint8_t* data, size_t len,
                            const Signature& sig) {
    return crypto_sign_verify_detached(sig.data(), data, len, pub.data()) == 0;
}

}  // namespace smartmic::security
