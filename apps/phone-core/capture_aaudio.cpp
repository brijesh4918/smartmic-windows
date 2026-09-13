// Android microphone capture via AAudio.
//
// AAudio is the NDK's low-latency audio API and needs no JNI, so the whole
// phone runtime stays in C++ and the Dart side only draws. Permission is still
// Android's business and is requested from Dart before the engine starts.
#if defined(__ANDROID__)

#include <aaudio/AAudio.h>
#include <android/log.h>

#include <atomic>
#include <vector>

#include "capture.h"
#include "smartmic/audio_format.h"
#include "smartmic/resampler.h"

namespace smartmic::phone {

namespace {
constexpr char kTag[] = "SmartMic";
void logi(const char* m) { __android_log_print(ANDROID_LOG_INFO, kTag, "%s", m); }
void loge(const char* m) { __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", m); }
}  // namespace

std::unique_ptr<ICapture> createToneCapture();

class AAudioCapture : public ICapture {
public:
    ~AAudioCapture() override { stop(); }

    bool start(FrameHandler handler) override {
        handler_ = std::move(handler);

        AAudioStreamBuilder* builder = nullptr;
        if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
            loge("AAudio_createStreamBuilder failed");
            return false;
        }

        AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
        AAudioStreamBuilder_setSampleRate(builder, (int32_t)kSampleRate);
        AAudioStreamBuilder_setChannelCount(builder, (int32_t)kChannels);
        AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
        // VOICE_COMMUNICATION gets the platform's echo cancellation and noise
        // suppression, which is what makes this usable in a meeting. It arrived
        // in API 28, which is why minSdk is 28 rather than AAudio's own 26:
        // an unprocessed microphone in a meeting is not worth the extra
        // device coverage.
        AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION);
        AAudioStreamBuilder_setDataCallback(builder, &AAudioCapture::onDataStatic, this);
        AAudioStreamBuilder_setErrorCallback(builder, &AAudioCapture::onErrorStatic, this);

        const aaudio_result_t rc = AAudioStreamBuilder_openStream(builder, &stream_);
        AAudioStreamBuilder_delete(builder);
        if (rc != AAUDIO_OK || stream_ == nullptr) {
            loge("AAudioStreamBuilder_openStream failed (microphone permission?)");
            stream_ = nullptr;
            return false;
        }

        // The device may refuse our request; honour what it actually gave us
        // rather than assuming, or every frame would be subtly wrong.
        deviceRate_ = AAudioStream_getSampleRate(stream_);
        deviceChannels_ = AAudioStream_getChannelCount(stream_);
        resampler_.reset((uint32_t)deviceRate_);
        pending_.clear();
        frame_.assign(kFrameSamples, 0.0f);

        char msg[128];
        snprintf(msg, sizeof(msg), "AAudio input open: %d Hz, %d ch", deviceRate_, deviceChannels_);
        logi(msg);

        if (AAudioStream_requestStart(stream_) != AAUDIO_OK) {
            loge("AAudioStream_requestStart failed");
            AAudioStream_close(stream_);
            stream_ = nullptr;
            return false;
        }
        running_.store(true);
        return true;
    }

    void stop() override {
        running_.store(false);
        if (stream_ != nullptr) {
            AAudioStream_requestStop(stream_);
            AAudioStream_close(stream_);
            stream_ = nullptr;
        }
    }

    const char* name() const override { return "AAudio"; }

private:
    static aaudio_data_callback_result_t onDataStatic(AAudioStream* /*stream*/, void* userData,
                                                      void* audioData, int32_t numFrames) {
        return static_cast<AAudioCapture*>(userData)->onData((const float*)audioData, numFrames);
    }

    static void onErrorStatic(AAudioStream* /*stream*/, void* userData, aaudio_result_t error) {
        loge("AAudio stream error; capture stopping");
        static_cast<AAudioCapture*>(userData)->running_.store(false);
        (void)error;
    }

    aaudio_data_callback_result_t onData(const float* input, int32_t numFrames) {
        if (!running_.load() || input == nullptr || numFrames <= 0) {
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

        // Downmix to mono first, then resample: the canonical format is mono,
        // and resampling per channel would be wasted work.
        mono_.resize((size_t)numFrames);
        if (deviceChannels_ == 1) {
            std::memcpy(mono_.data(), input, (size_t)numFrames * sizeof(float));
        } else {
            for (int32_t f = 0; f < numFrames; ++f) {
                float acc = 0.0f;
                for (int32_t c = 0; c < deviceChannels_; ++c) acc += input[f * deviceChannels_ + c];
                mono_[(size_t)f] = acc / (float)deviceChannels_;
            }
        }

        resampler_.process(mono_.data(), mono_.size(), pending_);

        // Re-frame to exactly 20 ms: AAudio's callback size is whatever the
        // device likes, and everything above here assumes canonical frames.
        while (pending_.size() >= kFrameSamples) {
            std::memcpy(frame_.data(), pending_.data(), kFrameSamples * sizeof(float));
            pending_.erase(pending_.begin(), pending_.begin() + (long)kFrameSamples);
            if (handler_) handler_(frame_.data(), kFrameSamples);
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    FrameHandler handler_;
    AAudioStream* stream_ = nullptr;
    std::atomic<bool> running_{false};
    int32_t deviceRate_ = (int32_t)kSampleRate;
    int32_t deviceChannels_ = 1;
    LinearResampler resampler_;
    std::vector<float> mono_, pending_, frame_;
};

std::unique_ptr<ICapture> createPlatformCapture() {
    auto capture = std::make_unique<AAudioCapture>();
    return capture;
}

}  // namespace smartmic::phone

#endif  // __ANDROID__
