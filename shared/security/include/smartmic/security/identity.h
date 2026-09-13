// Long-term device identity: an Ed25519 keypair per installation.
//
// The pairing code authenticates one pairing session (ADR-012). This key is
// what authenticates every connection afterwards, and what revocation revokes.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace smartmic::security {

// Must be called once per process before anything else here. Idempotent.
bool initCrypto();

inline constexpr size_t kPublicKeyBytes = 32;
inline constexpr size_t kSecretKeyBytes = 64;
inline constexpr size_t kSignatureBytes = 64;

using PublicKey = std::array<uint8_t, kPublicKeyBytes>;
using SecretKey = std::array<uint8_t, kSecretKeyBytes>;
using Signature = std::array<uint8_t, kSignatureBytes>;

std::string toBase64(const uint8_t* data, size_t len);
std::vector<uint8_t> fromBase64(const std::string& s);
std::string toHex(const uint8_t* data, size_t len);

class DeviceIdentity {
public:
    static DeviceIdentity generate();
    static std::optional<DeviceIdentity> fromSecretKey(const std::vector<uint8_t>& secret);

    const PublicKey& publicKey() const { return public_; }
    const SecretKey& secretKey() const { return secret_; }

    // Stable, public, and safe to put in discovery metadata or a QR code.
    std::string deviceId() const;
    // Short human-comparable form, shown in the UI next to a paired device.
    std::string fingerprint() const;

    Signature sign(const uint8_t* data, size_t len) const;
    Signature sign(const std::vector<uint8_t>& data) const { return sign(data.data(), data.size()); }

    static bool verify(const PublicKey& pub, const uint8_t* data, size_t len, const Signature& sig);
    static bool verify(const PublicKey& pub, const std::vector<uint8_t>& data, const Signature& sig) {
        return verify(pub, data.data(), data.size(), sig);
    }

    static std::string deviceIdFor(const PublicKey& pub);
    static std::string fingerprintFor(const PublicKey& pub);
    static std::optional<PublicKey> publicKeyFromDeviceId(const std::string& id);

private:
    PublicKey public_{};
    SecretKey secret_{};
};

}  // namespace smartmic::security
