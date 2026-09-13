#include "smartmic/protocol/codec.h"

namespace smartmic::protocol {

std::string_view toString(DecodeError e) {
    switch (e) {
        case DecodeError::None:               return "none";
        case DecodeError::NotJson:            return "not JSON";
        case DecodeError::NotAnObject:        return "not a JSON object";
        case DecodeError::MissingField:       return "missing required field";
        case DecodeError::BadFieldType:       return "field has the wrong type";
        case DecodeError::UnknownType:        return "unknown message type";
        case DecodeError::UnsupportedVersion: return "unsupported protocol version";
        case DecodeError::TooLarge:           return "message too large";
    }
    return "?";
}

std::string encode(const Message& m) {
    nlohmann::json j{
        {"protocolVersion", m.protocolVersion},
        {"sessionId", m.sessionId},
        {"messageId", m.messageId},
        {"seq", m.seq},
        {"timestamp", m.timestamp},
        {"deviceId", m.deviceId},
        {"type", std::string(toString(m.type))},
    };
    if (!m.payload.is_null() && !m.payload.empty()) j["payload"] = m.payload;
    return j.dump();
}

namespace {

template <typename T>
bool take(const nlohmann::json& j, const char* key, T& out, DecodeError& err) {
    const auto it = j.find(key);
    if (it == j.end()) { err = DecodeError::MissingField; return false; }
    try {
        out = it->get<T>();
    } catch (...) {
        err = DecodeError::BadFieldType;
        return false;
    }
    return true;
}

}  // namespace

DecodeResult decode(std::string_view text) {
    DecodeResult r;
    if (text.size() > kMaxMessageBytes) { r.error = DecodeError::TooLarge; return r; }

    // Never let the parser throw across the transport boundary: every input
    // here arrives from the network and is untrusted by definition.
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) { r.error = DecodeError::NotJson; return r; }
    if (!j.is_object())   { r.error = DecodeError::NotAnObject; return r; }

    Message m;
    DecodeError err = DecodeError::None;
    std::string typeName;
    if (!take(j, "protocolVersion", m.protocolVersion, err) ||
        !take(j, "sessionId", m.sessionId, err) ||
        !take(j, "messageId", m.messageId, err) ||
        !take(j, "seq", m.seq, err) ||
        !take(j, "timestamp", m.timestamp, err) ||
        !take(j, "deviceId", m.deviceId, err) ||
        !take(j, "type", typeName, err)) {
        r.error = err;
        return r;
    }

    // Version is checked before anything semantic is done with the message.
    if (m.protocolVersion < kMinimumAcceptedVersion || m.protocolVersion > kProtocolVersion) {
        r.error = DecodeError::UnsupportedVersion;
        return r;
    }

    const auto type = msgTypeFromString(typeName);
    if (!type) { r.error = DecodeError::UnknownType; return r; }
    m.type = *type;

    const auto payload = j.find("payload");
    if (payload != j.end()) {
        if (!payload->is_object()) { r.error = DecodeError::BadFieldType; return r; }
        m.payload = *payload;
    }

    r.message = std::move(m);
    return r;
}

DecodeResult decode(const std::vector<uint8_t>& bytes) {
    return decode(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}  // namespace smartmic::protocol
