// The audio router: the only component that decides what the world hears.
//
// Timing contract (test A14): every call to tick() produces exactly one
// kFrameSamples frame at the sink, in every state, including error states.
// The output stream never gaps, so nothing downstream can observe a transition
// as anything other than a change in content.
//
// Threading: control methods may be called from any thread. They enqueue
// events on a lock-free ring that tick() drains, so the audio thread never
// takes a lock, never allocates, and never waits.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "smartmic/audio_io.h"
#include "smartmic/crossfade.h"
#include "smartmic/device_registry.h"
#include "smartmic/ring_buffer.h"
#include "smartmic/source_state_machine.h"

namespace smartmic {

struct RouterConfig {
    uint32_t crossfadeMs        = 15;    // ADR-008; brief says 10-30 ms
    uint32_t armTimeoutMs       = 1500;  // PHONE_ARMING -> ErrorRecovery
    uint32_t phoneLostTimeoutMs = 600;   // PHONE_ACTIVE -> failback
    float    localGainLinear    = 1.0f;
    float    phoneGainLinear    = 1.0f;
};

struct RouterStats {
    uint64_t framesProduced   = 0;
    uint64_t transitions      = 0;
    uint64_t phoneLostEvents  = 0;
    uint64_t armTimeouts      = 0;
    uint64_t sourceFailures   = 0;
    uint64_t sinkFailures     = 0;
    uint64_t limitedSamples   = 0;  // samples the limiter had to pull back
    SourceState state = SourceState::LocalActive;
    RoutingMode mode  = RoutingMode::Auto;
};

class AudioRouter {
public:
    AudioRouter(RouterConfig config, DeviceRegistry* registry);

    // Wire-up. Call before the audio thread starts ticking.
    void setLocalSource(std::shared_ptr<IAudioSource> src);
    void setPhoneSource(std::shared_ptr<IAudioSource> src);
    void setSink(std::shared_ptr<IAudioSink> sink);

    // --- control surface (any thread, non-blocking) ---
    void setMode(RoutingMode mode);
    void startPtt();                 // idempotent
    void stopPtt();                  // idempotent, safe in any state
    void notifyPhoneReady();         // jitter buffer has real audio
    void notifyPhoneHeartbeat();     // liveness; also implied by media frames
    void notifyPhoneGone();          // explicit teardown (control channel closed)

    // --- audio thread ---
    // Produces exactly one frame. Returns false only if the sink rejected the
    // write; the router's own state still advances so a failing sink cannot
    // stall the state machine.
    bool tick();

    // The device the local source is expected to be capturing, per the
    // registry. Reported in diagnostics so a mismatch between what the router
    // thinks it is capturing and what the desktop app shows is visible rather
    // than silent.
    std::string effectiveInputId() const;
    std::string effectiveInputName() const;

    SourceState state() const { return sm_.state(); }
    RoutingMode mode() const { return sm_.mode(); }
    RouterStats stats() const;

private:
    void drainEvents();
    void applyEvent(Event e);
    void updateTimers();
    void readSources();
    void mixAndWrite();

    RouterConfig config_;
    DeviceRegistry* registry_;
    SourceStateMachine sm_;
    Crossfade fade_;
    GainRamp  gate_{1.0f};   // output gate: Mute / error recovery

    std::shared_ptr<IAudioSource> local_;
    std::shared_ptr<IAudioSource> phone_;
    std::shared_ptr<IAudioSink> sink_;

    std::vector<float> localBuf_;
    std::vector<float> phoneBuf_;
    std::vector<float> outBuf_;

    // Control events from other threads. Fixed capacity; if a caller manages to
    // overflow it the oldest event is dropped rather than blocking the caller,
    // and the watchdog still guarantees a safe state.
    RingBuffer<uint32_t> events_{256};

    // Frame-indexed clocks. Using the audio clock rather than wall time is what
    // makes the watchdog survive a wedged network thread (ADR-008).
    uint64_t frameIndex_ = 0;
    uint64_t armStartedFrame_ = 0;
    std::atomic<uint64_t> lastHeartbeatFrame_{0};

    std::atomic<uint64_t> framesProduced_{0};
    std::atomic<uint64_t> transitions_{0};
    std::atomic<uint64_t> phoneLost_{0};
    std::atomic<uint64_t> armTimeouts_{0};
    std::atomic<uint64_t> sourceFailures_{0};
    std::atomic<uint64_t> sinkFailures_{0};
    std::atomic<uint64_t> limited_{0};
};

}  // namespace smartmic
