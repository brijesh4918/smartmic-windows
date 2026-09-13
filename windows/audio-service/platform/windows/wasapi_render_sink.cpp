#include "wasapi_render_sink.h"

#include <avrt.h>

#include "smartmic/logging.h"

namespace smartmic {
namespace {
constexpr char kComponent[] = "WasapiRender";
constexpr REFERENCE_TIME kBufferDuration = 400000;  // 40 ms in 100 ns units
}

WasapiRenderSink::WasapiRenderSink(std::string id, std::string deviceId)
    : id_(std::move(id)), deviceId_(std::move(deviceId)) {}

WasapiRenderSink::~WasapiRenderSink() { stop(); }

bool WasapiRenderSink::start() {
    if (running_.load()) return true;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), enumerator_.putVoid()))) {
        return false;
    }
    const std::wstring wid = utf8ToWide(deviceId_);
    if (FAILED(enumerator_->GetDevice(wid.c_str(), device_.put()))) {
        logError(kComponent, "render device not found");
        return false;
    }
    if (FAILED(device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.putVoid()))) return false;
    if (FAILED(client_->GetMixFormat(&mixFormat_)) || !mixFormat_) return false;

    if (FAILED(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                   kBufferDuration, 0, mixFormat_, nullptr))) {
        logError(kComponent, "IAudioClient::Initialize failed");
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_ || FAILED(client_->SetEventHandle(event_))) return false;
    if (FAILED(client_->GetBufferSize(&bufferFrames_))) return false;
    if (FAILED(client_->GetService(__uuidof(IAudioRenderClient), render_.putVoid()))) return false;

    ring_.clear();
    running_.store(true);
    if (FAILED(client_->Start())) {
        running_.store(false);
        return false;
    }
    thread_ = std::thread([this] { renderLoop(); });
    return true;
}

void WasapiRenderSink::stop() {
    if (running_.exchange(false)) {
        if (event_) SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (client_) client_->Stop();
    }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mixFormat_) { CoTaskMemFree(mixFormat_); mixFormat_ = nullptr; }
    render_.reset();
    client_.reset();
    device_.reset();
    enumerator_.reset();
}

bool WasapiRenderSink::write(const float* in, size_t samples) {
    if (!running_.load()) return false;
    ring_.write(in, samples);
    return true;
}

void WasapiRenderSink::renderLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    while (running_.load()) {
        if (WaitForSingleObject(event_, 2000) != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        if (FAILED(client_->GetCurrentPadding(&padding))) continue;
        const UINT32 available = bufferFrames_ - padding;
        if (available == 0) continue;

        BYTE* out = nullptr;
        if (FAILED(render_->GetBuffer(available, &out))) continue;

        // Short reads become silence, exactly as the driver will behave when the
        // service starves it (ADR-002). Never stale audio, never a stall.
        scratch_.resize(available);
        ring_.readOrSilence(scratch_.data(), available);
        fromMonoFloat(scratch_.data(), available, *mixFormat_, out);
        render_->ReleaseBuffer(available, 0);
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

}  // namespace smartmic
