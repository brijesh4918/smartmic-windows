// Fallback capture: a tone. Used on desktop builds and whenever the platform
// capture cannot start, so the rest of the engine stays exercisable.
#include "capture.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "smartmic/audio_format.h"

namespace smartmic::phone {

class ToneCapture : public ICapture {
public:
    bool start(FrameHandler handler) override {
        handler_ = std::move(handler);
        running_.store(true);
        thread_ = std::thread([this] { run(); });
        return true;
    }
    void stop() override {
        if (running_.exchange(false) && thread_.joinable()) thread_.join();
    }
    const char* name() const override { return "tone (no microphone)"; }
    ~ToneCapture() override { stop(); }

private:
    void run() {
        std::vector<float> buf(kFrameSamples);
        double phase = 0.0;
        const double inc = 2.0 * 3.14159265358979 * 1093.0 / kSampleRate;
        auto next = std::chrono::steady_clock::now();
        while (running_.load()) {
            for (auto& v : buf) { v = 0.4f * static_cast<float>(std::sin(phase)); phase += inc; }
            if (handler_) handler_(buf.data(), buf.size());
            next += std::chrono::milliseconds(kFrameMillis);
            std::this_thread::sleep_until(next);
        }
    }

    FrameHandler handler_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

#if !defined(__ANDROID__) && !defined(__APPLE__)
std::unique_ptr<ICapture> createPlatformCapture() {
    return std::make_unique<ToneCapture>();
}
#endif

std::unique_ptr<ICapture> createToneCapture() { return std::make_unique<ToneCapture>(); }

}  // namespace smartmic::phone
