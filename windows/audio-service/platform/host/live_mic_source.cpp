#include "live_mic_source.h"

#include <algorithm>

#include "smartmic/logging.h"

namespace smartmic::host {

LiveMicSource::LiveMicSource(std::string id) : id_(std::move(id)) {}
LiveMicSource::~LiveMicSource() { stop(); }

const char* LiveMicSource::backend() const {
    return capture_ ? capture_->name() : "none";
}

bool LiveMicSource::start() {
    capture_ = phone::createPlatformCapture();
    if (capture_ == nullptr) return false;

    const bool ok = capture_->start([this](const float* pcm, size_t n) {
        ring_.write(pcm, n);
    });
    live_.store(ok);
    if (!ok) {
        logWarn("LiveMic", "could not open the host microphone; check the permission");
    } else {
        logInfo("LiveMic", std::string("capturing via ") + capture_->name());
    }
    return ok;
}

void LiveMicSource::stop() {
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }
    live_.store(false);
}

SourceStatus LiveMicSource::read(float* out, size_t samples) {
    if (!live_.load()) {
        std::fill(out, out + samples, 0.0f);
        return SourceStatus::Failed;
    }
    const size_t got = ring_.readOrSilence(out, samples);
    return got == samples ? SourceStatus::Ok : SourceStatus::Underrun;
}

}  // namespace smartmic::host
