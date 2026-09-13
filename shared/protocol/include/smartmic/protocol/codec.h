#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "smartmic/protocol/message.h"

namespace smartmic::protocol {

enum class DecodeError {
    None,
    NotJson,
    NotAnObject,
    MissingField,
    BadFieldType,
    UnknownType,
    UnsupportedVersion,
    TooLarge,
};

std::string_view toString(DecodeError e);

// Hard cap. A control message is a few hundred bytes; anything approaching this
// is either a bug or an attempt to make the parser work hard on our behalf.
inline constexpr size_t kMaxMessageBytes = 8 * 1024;

std::string encode(const Message& m);

struct DecodeResult {
    DecodeError error = DecodeError::None;
    Message message;
    explicit operator bool() const { return error == DecodeError::None; }
};

DecodeResult decode(std::string_view text);
DecodeResult decode(const std::vector<uint8_t>& bytes);

}  // namespace smartmic::protocol
