// iOS / macOS microphone capture via AudioUnit (the C layer under
// AVAudioEngine). Using the AudioUnit API directly keeps the phone runtime in
// C++ on both mobile platforms, so there is one implementation of the PTT
// timing rules rather than two.
#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "capture.h"
#include "smartmic/audio_format.h"
#include "smartmic/resampler.h"

namespace smartmic::phone {

class CoreAudioCapture : public ICapture {
public:
    ~CoreAudioCapture() override { stop(); }

    bool start(FrameHandler handler) override {
        handler_ = std::move(handler);

        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
        desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
#else
        desc.componentSubType = kAudioUnitSubType_HALOutput;
#endif
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;

        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        if (comp == nullptr || AudioComponentInstanceNew(comp, &unit_) != noErr) return false;

        UInt32 enable = 1;
        if (AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                                 kAudioUnitScope_Input, 1, &enable, sizeof(enable)) != noErr) {
            stop();
            return false;
        }
        UInt32 disable = 0;
        AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Output, 0, &disable, sizeof(disable));

        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = (Float64)kSampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mFramesPerPacket = 1;
        fmt.mChannelsPerFrame = 1;
        fmt.mBitsPerChannel = 32;
        fmt.mBytesPerFrame = 4;
        fmt.mBytesPerPacket = 4;
        if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                                 kAudioUnitScope_Output, 1, &fmt, sizeof(fmt)) != noErr) {
            stop();
            return false;
        }

        AURenderCallbackStruct cb{};
        cb.inputProc = &CoreAudioCapture::inputProc;
        cb.inputProcRefCon = this;
        AudioUnitSetProperty(unit_, kAudioOutputUnitProperty_SetInputCallback,
                             kAudioUnitScope_Global, 0, &cb, sizeof(cb));

        if (AudioUnitInitialize(unit_) != noErr || AudioOutputUnitStart(unit_) != noErr) {
            stop();
            return false;
        }

        pending_.clear();
        frame_.assign(kFrameSamples, 0.0f);
        running_.store(true);
        return true;
    }

    void stop() override {
        running_.store(false);
        if (unit_ != nullptr) {
            AudioOutputUnitStop(unit_);
            AudioUnitUninitialize(unit_);
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
        }
    }

    const char* name() const override { return "AudioUnit"; }

private:
    static OSStatus inputProc(void* refCon, AudioUnitRenderActionFlags* flags,
                              const AudioTimeStamp* timeStamp, UInt32 bus,
                              UInt32 numFrames, AudioBufferList*) {
        return static_cast<CoreAudioCapture*>(refCon)->render(flags, timeStamp, bus, numFrames);
    }

    OSStatus render(AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts,
                    UInt32 bus, UInt32 numFrames) {
        if (!running_.load()) return noErr;

        scratch_.resize(numFrames);
        AudioBufferList list{};
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = 1;
        list.mBuffers[0].mDataByteSize = numFrames * sizeof(float);
        list.mBuffers[0].mData = scratch_.data();

        const OSStatus status = AudioUnitRender(unit_, flags, ts, bus, numFrames, &list);
        if (status != noErr) return status;

        pending_.insert(pending_.end(), scratch_.begin(), scratch_.end());
        while (pending_.size() >= kFrameSamples) {
            std::memcpy(frame_.data(), pending_.data(), kFrameSamples * sizeof(float));
            pending_.erase(pending_.begin(), pending_.begin() + (long)kFrameSamples);
            if (handler_) handler_(frame_.data(), kFrameSamples);
        }
        return noErr;
    }

    FrameHandler handler_;
    AudioUnit unit_ = nullptr;
    std::atomic<bool> running_{false};
    std::vector<float> scratch_, pending_, frame_;
};

std::unique_ptr<ICapture> createPlatformCapture() {
    return std::make_unique<CoreAudioCapture>();
}

}  // namespace smartmic::phone

#endif  // __APPLE__
