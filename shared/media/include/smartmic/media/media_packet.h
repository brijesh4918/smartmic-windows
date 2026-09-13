// Media packet header. Fixed size, little-endian, no variable-length fields:
// this is parsed from the network on every frame and must be impossible to get
// wrong.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace smartmic::media {

inline constexpr uint8_t kMediaVersion = 1;

enum MediaFlags : uint8_t {
    kFlagNone = 0,
    kFlagPttActive = 1 << 0,   // doubles as liveness while PTT is held
    kFlagMarker = 1 << 1,      // first packet of a talk spurt
};

#pragma pack(push, 1)
struct MediaHeader {
    uint8_t  version;
    uint8_t  flags;
    uint16_t reserved;
    uint32_t seq;              // per-session, monotonic, wraps are handled
    uint32_t timestampSamples; // canonical-rate sample clock
};
#pragma pack(pop)

static_assert(sizeof(MediaHeader) == 12, "MediaHeader must be exactly 12 bytes");

inline void writeHeader(const MediaHeader& h, std::vector<uint8_t>& out) {
    const size_t at = out.size();
    out.resize(at + sizeof(MediaHeader));
    std::memcpy(out.data() + at, &h, sizeof(MediaHeader));
}

// Returns false for anything too short or of an unknown version. Never reads
// past `len`.
inline bool readHeader(const uint8_t* data, size_t len, MediaHeader& out) {
    if (len < sizeof(MediaHeader)) return false;
    std::memcpy(&out, data, sizeof(MediaHeader));
    return out.version == kMediaVersion;
}

}  // namespace smartmic::media
