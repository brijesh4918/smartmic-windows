// Source / sink abstractions. Everything above this line is portable; every
// platform audio API lives below it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "smartmic/audio_format.h"

namespace smartmic {

enum class SourceStatus {
    Ok,        // frame delivered in full from real audio
    Underrun,  // partially or wholly zero-filled, but the source is alive
    Failed,    // the source is gone; the router must stop relying on it
};

// A source of canonical audio. Implementations MUST always write exactly
// `samples` values (zero-filling any shortfall) so the router's output timing
// can never depend on a source's behaviour.
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual const std::string& id() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual SourceStatus read(float* out, size_t samples) = 0;
};

// A destination for canonical audio: in Phase 1 a WAV file or a development
// virtual cable; from Phase 5 the SmartMic driver ring.
class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual const std::string& id() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool write(const float* in, size_t samples) = 0;
};

// Marker interface for the thing that ultimately presents "Smart Microphone"
// to Windows. VirtualCableDriverLink (Phase 1, dev only) and SmartMicDriverLink
// (Phase 5) both implement it; the router cannot tell them apart.
class IDriverLink : public IAudioSink {
public:
    virtual bool endpointReady() const = 0;
    virtual uint64_t underrunCount() const = 0;
};

struct DeviceInfo {
    std::string id;
    std::string name;
    bool isDefault = false;
    // True for the SmartMic endpoint itself. The registry must never offer
    // this as a capture source -- see ADR-001 and test A12.
    bool isSmartMicEndpoint = false;
    uint32_t nativeRate = kSampleRate;
    uint32_t nativeChannels = kChannels;
};

}  // namespace smartmic
