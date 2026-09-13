#include "smartmic/protocol/message.h"

#include <sodium.h>

#include <array>
#include <chrono>
#include <cstdio>

namespace smartmic::protocol {
namespace {
struct NameMap { MsgType t; std::string_view s; };
constexpr NameMap kNames[] = {
    {MsgType::Hello, "HELLO"}, {MsgType::Auth, "AUTH"},
    {MsgType::Capabilities, "CAPABILITIES"}, {MsgType::Ping, "PING"},
    {MsgType::Pong, "PONG"}, {MsgType::StartPtt, "START_PTT"},
    {MsgType::PttPreparing, "PTT_PREPARING"}, {MsgType::PttReady, "PTT_READY"},
    {MsgType::StopPtt, "STOP_PTT"}, {MsgType::PttStopped, "PTT_STOPPED"},
    {MsgType::SetMode, "SET_MODE"}, {MsgType::SetLocalSource, "SET_LOCAL_SOURCE"},
    {MsgType::GetStatus, "GET_STATUS"}, {MsgType::Status, "STATUS"},
    {MsgType::Error, "ERROR"},
};
}  // namespace

std::string_view toString(MsgType t) {
    for (const auto& n : kNames) if (n.t == t) return n.s;
    return "?";
}

std::optional<MsgType> msgTypeFromString(std::string_view s) {
    for (const auto& n : kNames) if (n.s == s) return n.t;
    return std::nullopt;
}

std::string_view toString(ErrorCode c) {
    switch (c) {
        case ErrorCode::Unauthenticated:  return "UNAUTHENTICATED";
        case ErrorCode::Revoked:          return "REVOKED";
        case ErrorCode::VersionMismatch:  return "VERSION_MISMATCH";
        case ErrorCode::PairingExpired:   return "PAIRING_EXPIRED";
        case ErrorCode::PairingThrottled: return "PAIRING_THROTTLED";
        case ErrorCode::InvalidSource:    return "INVALID_SOURCE";
        case ErrorCode::ArmTimeout:       return "ARM_TIMEOUT";
        case ErrorCode::Internal:         return "INTERNAL";
    }
    return "INTERNAL";
}

uint64_t nowUnixMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::string newMessageId() {
    // UUIDv4 from the CSPRNG. Message ids are not secrets, but they must not be
    // predictable enough to help an attacker line up a replay.
    std::array<unsigned char, 16> b{};
    randombytes_buf(b.data(), b.size());
    b[6] = static_cast<unsigned char>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3F) | 0x80);
    char out[37];
    std::snprintf(out, sizeof(out),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                  b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(out);
}

Message Message::make(MsgType type, const std::string& sessionId, const std::string& deviceId,
                      uint64_t seq, nlohmann::json payload) {
    Message m;
    m.protocolVersion = kProtocolVersion;
    m.sessionId = sessionId;
    m.messageId = newMessageId();
    m.seq = seq;
    m.timestamp = nowUnixMs();
    m.deviceId = deviceId;
    m.type = type;
    m.payload = std::move(payload);
    return m;
}

Message makeError(const std::string& sessionId, const std::string& deviceId, uint64_t seq,
                  ErrorCode code, std::string message) {
    return Message::make(MsgType::Error, sessionId, deviceId, seq,
                         {{"code", std::string(toString(code))}, {"message", std::move(message)}});
}

Message makePttReady(const std::string& sessionId, const std::string& deviceId, uint64_t seq,
                     uint64_t acceptedAtMs) {
    return Message::make(MsgType::PttReady, sessionId, deviceId, seq,
                         {{"acceptedAt", acceptedAtMs}, {"routerState", "PHONE_ACTIVE"}});
}

}  // namespace smartmic::protocol
