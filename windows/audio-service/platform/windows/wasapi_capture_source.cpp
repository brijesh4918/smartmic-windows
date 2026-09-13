#include "wasapi_capture_source.h"

#include <avrt.h>

#include "smartmic/logging.h"

namespace smartmic {
namespace {
constexpr char kComponent[] = "WasapiCapture";
constexpr REFERENCE_TIME kBufferDuration = 200000;  // 20 ms in 100 ns units
}

WasapiCaptureSource::WasapiCaptureSource(std::string id, std::string deviceId)
    : id_(std::move(id)), deviceId_(std::move(deviceId)) {}

WasapiCaptureSource::~WasapiCaptureSource() { stop(); }

bool WasapiCaptureSource::start() {
    if (running_.load()) return true;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), enumerator_.putVoid());
    if (FAILED(hr)) return false;

    const std::wstring wid = utf8ToWide(deviceId_);
    hr = enumerator_->GetDevice(wid.c_str(), device_.put());
    if (FAILED(hr)) {
        logError(kComponent, "capture device not found");
        return false;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.putVoid());
    if (FAILED(hr)) return false;

    hr = client_->GetMixFormat(&mixFormat_);
    if (FAILED(hr) || !mixFormat_) return false;

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                             kBufferDuration, 0, mixFormat_, nullptr);
    if (FAILED(hr)) {
        logError(kComponent, "IAudioClient::Initialize failed");
        return false;
    }

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_ || FAILED(client_->SetEventHandle(event_))) return false;
    if (FAILED(client_->GetService(__uuidof(IAudioCaptureClient), capture_.putVoid()))) return false;

    resampler_.reset(mixFormat_->nSamplesPerSec);
    ring_.clear();
    failed_.store(false);
    running_.store(true);

    if (FAILED(client_->Start())) {
        running_.store(false);
        return false;
    }
    thread_ = std::thread([this] { captureLoop(); });
    logInfo(kComponent, "capturing '" + deviceId_ + "' at " +
                            std::to_string(mixFormat_->nSamplesPerSec) + " Hz, " +
                            std::to_string(mixFormat_->nChannels) + " ch");
    return true;
}

void WasapiCaptureSource::stop() {
    if (running_.exchange(false)) {
        if (event_) SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (client_) client_->Stop();
    }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    if (mixFormat_) { CoTaskMemFree(mixFormat_); mixFormat_ = nullptr; }
    capture_.reset();
    client_.reset();
    device_.reset();
    enumerator_.reset();
}

void WasapiCaptureSource::captureLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Join the Pro Audio MMCSS task so the capture thread is scheduled ahead of
    // ordinary work; without it, a busy machine produces audible dropouts.
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    while (running_.load()) {
        if (WaitForSingleObject(event_, 2000) != WAIT_OBJECT_0) {
            if (running_.load()) {
                logWarn(kComponent, "capture event timed out");
                failed_.store(true);
            }
            continue;
        }
        UINT32 packet = 0;
        while (SUCCEEDED(capture_->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;

            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                scratch_.assign(frames, 0.0f);
            } else {
                toMonoFloat(data, frames, *mixFormat_, scratch_);
            }
            capture_->ReleaseBuffer(frames);

            converted_.clear();
            resampler_.process(scratch_.data(), scratch_.size(), converted_);
            if (!converted_.empty()) ring_.write(converted_.data(), converted_.size());
        }
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    CoUninitialize();
}

SourceStatus WasapiCaptureSource::read(float* out, size_t samples) {
    if (failed_.load()) {
        std::fill(out, out + samples, 0.0f);
        return SourceStatus::Failed;
    }
    const size_t got = ring_.readOrSilence(out, samples);
    if (!running_.load()) return SourceStatus::Failed;
    return got == samples ? SourceStatus::Ok : SourceStatus::Underrun;
}

}  // namespace smartmic
