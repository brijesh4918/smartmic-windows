#include "smartmic/router.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "smartmic/logging.h"

namespace smartmic {
namespace {
constexpr char kComponent[] = "AudioRouter";
constexpr float kCeiling = 0.999f;

uint64_t framesFor(uint32_t ms) {
    return std::max<uint64_t>(1, ms / kFrameMillis);
}
}  // namespace

AudioRouter::AudioRouter(RouterConfig config, DeviceRegistry* registry)
    : config_(config),
      registry_(registry),
      localBuf_(kFrameSamples, 0.0f),
      phoneBuf_(kFrameSamples, 0.0f),
      outBuf_(kFrameSamples, 0.0f) {
    fade_.snapTo(0.0f);
}

void AudioRouter::setLocalSource(std::shared_ptr<IAudioSource> src) { local_ = std::move(src); }
void AudioRouter::setPhoneSource(std::shared_ptr<IAudioSource> src) { phone_ = std::move(src); }
void AudioRouter::setSink(std::shared_ptr<IAudioSink> sink) { sink_ = std::move(sink); }

// --- control surface -------------------------------------------------------
// These run on arbitrary threads. They do nothing but post an event; all
// decisions happen on the audio thread inside tick().

namespace {
void post(RingBuffer<uint32_t>& q, Event e) {
    const auto v = static_cast<uint32_t>(e);
    q.write(&v, 1);
}
}  // namespace

void AudioRouter::setMode(RoutingMode mode) {
    switch (mode) {
        case RoutingMode::Auto:      post(events_, Event::ModeAuto); break;
        case RoutingMode::PhoneOnly: post(events_, Event::ModePhoneOnly); break;
        case RoutingMode::LocalOnly: post(events_, Event::ModeLocalOnly); break;
        case RoutingMode::Mute:      post(events_, Event::ModeMute); break;
    }
}

void AudioRouter::startPtt() {
    // Treat the request itself as liveness, so a press cannot immediately trip
    // the watchdog that the press is about to arm.
    lastHeartbeatFrame_.store(framesProduced_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    post(events_, Event::StartPtt);
}

void AudioRouter::stopPtt() { post(events_, Event::StopPtt); }

void AudioRouter::notifyPhoneReady() {
    lastHeartbeatFrame_.store(framesProduced_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    post(events_, Event::PhoneReady);
}

void AudioRouter::notifyPhoneHeartbeat() {
    lastHeartbeatFrame_.store(framesProduced_.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

void AudioRouter::notifyPhoneGone() { post(events_, Event::PhoneLost); }

// --- audio thread ----------------------------------------------------------

bool AudioRouter::tick() {
    drainEvents();
    updateTimers();
    readSources();
    mixAndWrite();

    ++frameIndex_;
    framesProduced_.store(frameIndex_, std::memory_order_relaxed);

    bool ok = true;
    if (sink_) {
        ok = sink_->write(outBuf_.data(), kFrameSamples);
        if (!ok) sinkFailures_.fetch_add(1, std::memory_order_relaxed);
    }

    // The fade for a release finishes inside mixAndWrite(); closing the state
    // here keeps the frame-per-tick contract exact.
    if (sm_.state() == SourceState::ReturningToLocal && !fade_.active()) {
        applyEvent(Event::FadeComplete);
    }
    return ok;
}

void AudioRouter::drainEvents() {
    uint32_t raw = 0;
    while (events_.read(&raw, 1) == 1) {
        applyEvent(static_cast<Event>(raw));
    }
}

void AudioRouter::applyEvent(Event e) {
    const Transition t = sm_.apply(e);
    if (!t.accepted) {
        logDebug(kComponent, std::string("rejected ") + std::string(toString(e)) +
                                 " in " + std::string(toString(t.from)));
        return;
    }
    if (!t.changed) return;

    transitions_.fetch_add(1, std::memory_order_relaxed);
    logInfo(kComponent, std::string(toString(t.from)) + " -> " + std::string(toString(t.to)) +
                            " on " + std::string(toString(e)));

    // The gate is opened or closed on a ramp, never as a step.
    const bool silent = (t.to == SourceState::Muted || t.to == SourceState::ErrorRecovery);
    gate_.rampTo(silent ? 0.0f : 1.0f, config_.crossfadeMs);

    switch (t.to) {
        case SourceState::PhoneActive:
        case SourceState::PhoneOnly:
            fade_.fadeTo(1.0f, config_.crossfadeMs);
            break;
        case SourceState::PhoneArming:
            armStartedFrame_ = frameIndex_;
            fade_.fadeTo(0.0f, config_.crossfadeMs);  // stay on local until ready
            break;
        case SourceState::ReturningToLocal:
            fade_.fadeTo(0.0f, config_.crossfadeMs);
            break;
        case SourceState::LocalActive:
        case SourceState::LocalOnly:
            fade_.fadeTo(0.0f, config_.crossfadeMs);
            break;
        case SourceState::Muted:
            // Gated silent above; still move the crossfade home so that
            // unmuting resumes from a defined position.
            fade_.fadeTo(0.0f, config_.crossfadeMs);
            break;
        case SourceState::ErrorRecovery:
            // Leave the crossfade where it is: recovery is a single frame and
            // the gate is what makes it silent.
            break;
    }
}

void AudioRouter::updateTimers() {
    // Error recovery is a single-frame state: it exists so that "something went
    // wrong" is observable and logged, not so the router sits in it.
    if (sm_.state() == SourceState::ErrorRecovery) {
        applyEvent(Event::Recovered);
        return;
    }

    if (sm_.state() == SourceState::PhoneArming &&
        frameIndex_ - armStartedFrame_ > framesFor(config_.armTimeoutMs)) {
        armTimeouts_.fetch_add(1, std::memory_order_relaxed);
        logWarn(kComponent, "phone arming timed out; returning to local");
        applyEvent(Event::ArmTimeout);
        return;
    }

    if (sm_.state() == SourceState::PhoneActive) {
        const uint64_t last = lastHeartbeatFrame_.load(std::memory_order_relaxed);
        if (frameIndex_ > last && frameIndex_ - last > framesFor(config_.phoneLostTimeoutMs)) {
            phoneLost_.fetch_add(1, std::memory_order_relaxed);
            logWarn(kComponent, "phone liveness lost; failing back to local microphone");
            applyEvent(Event::PhoneLost);
        }
    }
}

void AudioRouter::readSources() {
    auto fill = [&](const std::shared_ptr<IAudioSource>& src, bool wanted, std::vector<float>& buf) {
        if (!wanted || !src) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            return;
        }
        const SourceStatus st = src->read(buf.data(), kFrameSamples);
        if (st == SourceStatus::Failed) {
            sourceFailures_.fetch_add(1, std::memory_order_relaxed);
            std::fill(buf.begin(), buf.end(), 0.0f);
            logError(kComponent, "source '" + src->id() + "' failed");
            applyEvent(Event::SourceFailure);
        }
    };
    // While the output gate is still moving, keep reading whatever was feeding
    // it. Otherwise entering Mute zeroes the buffers on the same sample the
    // gate starts closing, and the ramp has nothing left to fade -- which is a
    // hard cut wearing a crossfade's clothes.
    const bool holdForGate = gate_.active();
    fill(local_, sm_.wantsLocal() || holdForGate, localBuf_);
    fill(phone_, sm_.wantsPhone() || holdForGate, phoneBuf_);
}

void AudioRouter::mixAndWrite() {
    uint64_t limited = 0;
    for (size_t i = 0; i < kFrameSamples; ++i) {
        const GainPair g = fade_.next();
        float s = localBuf_[i] * g.local * config_.localGainLinear +
                  phoneBuf_[i] * g.phone * config_.phoneGainLinear;
        s *= gate_.next();
        if (s > kCeiling)       { s = kCeiling;  ++limited; }
        else if (s < -kCeiling) { s = -kCeiling; ++limited; }
        outBuf_[i] = s;
    }
    if (limited) limited_.fetch_add(limited, std::memory_order_relaxed);
}

std::string AudioRouter::effectiveInputId() const {
    if (!registry_) return {};
    const auto d = registry_->resolveEffectiveInput();
    return d ? d->id : std::string{};
}

std::string AudioRouter::effectiveInputName() const {
    if (!registry_) return "(none)";
    const auto d = registry_->resolveEffectiveInput();
    return d ? d->name : std::string("(none)");
}

RouterStats AudioRouter::stats() const {
    RouterStats s;
    s.framesProduced  = framesProduced_.load(std::memory_order_relaxed);
    s.transitions     = transitions_.load(std::memory_order_relaxed);
    s.phoneLostEvents = phoneLost_.load(std::memory_order_relaxed);
    s.armTimeouts     = armTimeouts_.load(std::memory_order_relaxed);
    s.sourceFailures  = sourceFailures_.load(std::memory_order_relaxed);
    s.sinkFailures    = sinkFailures_.load(std::memory_order_relaxed);
    s.limitedSamples  = limited_.load(std::memory_order_relaxed);
    s.state = sm_.state();
    s.mode  = sm_.mode();
    return s;
}

}  // namespace smartmic
