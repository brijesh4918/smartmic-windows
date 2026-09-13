#include "wasapi_common.h"

#include <mmreg.h>

#include <algorithm>
#include <cstring>

namespace smartmic {
namespace {

bool isFloatFormat(const WAVEFORMATEX& fmt) {
    if (fmt.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (fmt.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto& ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(fmt);
        return IsEqualGUID(ext.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
    }
    return false;
}

}  // namespace

std::string wideToUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

void toMonoFloat(const BYTE* src, UINT32 frames, const WAVEFORMATEX& fmt, std::vector<float>& out) {
    out.resize(frames);
    const uint16_t ch = fmt.nChannels ? fmt.nChannels : 1;
    if (isFloatFormat(fmt) && fmt.wBitsPerSample == 32) {
        const auto* f = reinterpret_cast<const float*>(src);
        for (UINT32 i = 0; i < frames; ++i) {
            float acc = 0.0f;
            for (uint16_t c = 0; c < ch; ++c) acc += f[i * ch + c];
            out[i] = acc / static_cast<float>(ch);
        }
        return;
    }
    if (fmt.wBitsPerSample == 16) {
        const auto* s = reinterpret_cast<const int16_t*>(src);
        for (UINT32 i = 0; i < frames; ++i) {
            int acc = 0;
            for (uint16_t c = 0; c < ch; ++c) acc += s[i * ch + c];
            out[i] = static_cast<float>(acc) / (32768.0f * static_cast<float>(ch));
        }
        return;
    }
    if (fmt.wBitsPerSample == 32) {  // int32
        const auto* s = reinterpret_cast<const int32_t*>(src);
        for (UINT32 i = 0; i < frames; ++i) {
            double acc = 0.0;
            for (uint16_t c = 0; c < ch; ++c) acc += s[i * ch + c];
            out[i] = static_cast<float>(acc / (2147483648.0 * ch));
        }
        return;
    }
    // Unsupported bit depth: silence rather than noise. The enumerator refuses
    // such devices up front, so this is belt and braces.
    std::fill(out.begin(), out.end(), 0.0f);
}

void fromMonoFloat(const float* src, UINT32 frames, const WAVEFORMATEX& fmt, BYTE* dst) {
    const uint16_t ch = fmt.nChannels ? fmt.nChannels : 1;
    if (isFloatFormat(fmt) && fmt.wBitsPerSample == 32) {
        auto* f = reinterpret_cast<float*>(dst);
        for (UINT32 i = 0; i < frames; ++i)
            for (uint16_t c = 0; c < ch; ++c) f[i * ch + c] = src[i];
        return;
    }
    if (fmt.wBitsPerSample == 16) {
        auto* s = reinterpret_cast<int16_t*>(dst);
        for (UINT32 i = 0; i < frames; ++i) {
            const float v = std::clamp(src[i], -1.0f, 1.0f);
            const auto q = static_cast<int16_t>(v * 32767.0f);
            for (uint16_t c = 0; c < ch; ++c) s[i * ch + c] = q;
        }
        return;
    }
    std::memset(dst, 0, static_cast<size_t>(frames) * fmt.nBlockAlign);
}

bool looksLikeSmartMicEndpoint(const std::string& id, const std::string& name) {
    // Phase 5 replaces this with an exact match on the driver's own device
    // interface GUID, which is authoritative. Until that driver exists, a name
    // match is the available signal -- and it is backed by the router's own
    // refusal to capture its sink, so a false negative here is not fatal.
    auto contains = [](const std::string& hay, const char* needle) {
        std::string h = hay, n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };
    return contains(name, "smart microphone") || contains(name, "smartmic") || contains(id, "smartmic");
}

}  // namespace smartmic
