// SmartMic control protocol v1 -- C++ binding.
//
// The wire format is defined by shared/protocol/control-v1.schema.json. This
// header is generated-by-hand from it; the schema stays authoritative and the
// round-trip tests check the two against a shared set of test vectors.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace smartmic::protocol {

inline constexpr uint32_t kProtocolVersion = 1;

// Anything older than this is refused rather than negotiated down: a downgrade
// is an attack, not a compatibility feature (threat model T15).
inline constexpr uint32_t kMinimumAcceptedVersion = 1;

enum class MsgType {
    Hello, Auth, Capabilities, Ping, Pong,
    StartPtt, PttPreparing, PttReady, StopPtt, PttStopped,
    SetMode, SetLocalSource, GetStatus, Status, Error,
};

std::string_view toString(MsgType t);
std::optional<MsgType> msgTypeFromString(std::string_view s);

enum class ErrorCode {
    Unauthenticated, Revoked, VersionMismatch, PairingExpired,
    PairingThrottled, InvalidSource, ArmTimeout, Internal,
};
std::string_view toString(ErrorCode c);

struct Message {
    uint32_t protocolVersion = kProtocolVersion;
    std::string sessionId;
    std::string messageId;
    uint64_t seq = 0;
    uint64_t timestamp = 0;   // unix ms
    std::string deviceId;
    MsgType type = MsgType::Ping;
    nlohmann::json payload = nlohmann::json::object();

    static Message make(MsgType type, const std::string& sessionId,
                        const std::string& deviceId, uint64_t seq,
                        nlohmann::json payload = nlohmann::json::object());
};

// Convenience builders for the messages with required payload fields, so a
// caller cannot forget one and discover it at the far end.
Message makeError(const std::string& sessionId, const std::string& deviceId, uint64_t seq,
                  ErrorCode code, std::string message);
Message makePttReady(const std::string& sessionId, const std::string& deviceId, uint64_t seq,
                     uint64_t acceptedAtMs);

uint64_t nowUnixMs();
std::string newMessageId();

}  // namespace smartmic::protocol
