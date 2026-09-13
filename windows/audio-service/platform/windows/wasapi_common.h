// Shared WASAPI helpers.
//
// NOTE: everything in platform/windows/ compiles only with MSVC and the Windows
// SDK. It is excluded from the build on other hosts by CMake, which is what
// lets the router and its tests be developed and run anywhere.
#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <string>
#include <vector>

#include "smartmic/audio_format.h"

namespace smartmic {

// Minimal COM smart pointer: the service links no COM helper library, and the
// audio path should not depend on one.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

    T** put() { reset(); return &p_; }
    void** putVoid() { reset(); return reinterpret_cast<void**>(&p_); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }

private:
    T* p_ = nullptr;
};

std::string wideToUtf8(const wchar_t* w);
std::wstring utf8ToWide(const std::string& s);

// Converts an arbitrary WASAPI buffer to canonical mono float32 (no rate
// conversion -- the caller's resampler does that).
void toMonoFloat(const BYTE* src, UINT32 frames, const WAVEFORMATEX& fmt, std::vector<float>& out);

// Converts canonical mono float32 to the device's mix format.
void fromMonoFloat(const float* src, UINT32 frames, const WAVEFORMATEX& fmt, BYTE* dst);

// True when the endpoint's friendly name / id identifies SmartMic's own
// virtual microphone. Used to keep it out of the capture inventory (ADR-001).
bool looksLikeSmartMicEndpoint(const std::string& id, const std::string& name);

}  // namespace smartmic
