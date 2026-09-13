#include <algorithm>
#include <cmath>
#include <memory>

#include "host_sources.h"
#include "smartmic/router.h"
#include "test_harness.h"

using namespace smartmic;
using namespace smartmic::host;

namespace {

struct Rig {
    DeviceRegistry registry;
    std::shared_ptr<ToneSource> local;
    std::shared_ptr<ToneSource> phone;
    std::shared_ptr<MemorySink> sink;
    std::unique_ptr<AudioRouter> router;

    explicit Rig(float localHz = 317.0f, float phoneHz = 1093.0f, float amp = 0.7f,
                 RouterConfig cfg = RouterConfig{}) {
        local = std::make_shared<ToneSource>("local", localHz, amp);
        // Deliberately non-harmonic frequencies, a phase offset and unequal
        // amplitudes: with harmonically related tones the two sources can share
        // a zero crossing at the switch instant and hide a hard cut entirely.
        phone = std::make_shared<ToneSource>("phone", phoneHz, amp * 0.6f);
        phone->setPhase(1.1);
        sink = std::make_shared<MemorySink>("sink");
        local->start();
        phone->start();
        sink->start();
        router = std::make_unique<AudioRouter>(cfg, &registry);
        router->setLocalSource(local);
        router->setPhoneSource(phone);
        router->setSink(sink);
    }

    void tick(int n) { for (int i = 0; i < n; ++i) router->tick(); }

    // Drives a full press: request, let the router arm, acknowledge readiness.
    void pressAndAck() {
        router->startPtt();
        router->tick();
        router->notifyPhoneReady();
        router->tick();
    }
};

double maxStep(const std::vector<float>& s, size_t from, size_t to) {
    double m = 0.0;
    to = std::min(to, s.size());
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i) {
        m = std::max(m, std::fabs(static_cast<double>(s[i] - s[i - 1])));
    }
    return m;
}

}  // namespace

SM_TEST(A14, "the router emits exactly one frame per tick in every state") {
    Rig rig;
    auto expectFrames = [&](size_t ticks) {
        CHECK_EQ(rig.sink->samples().size(), ticks * kFrameSamples);
        CHECK_EQ(rig.sink->writeCount(), ticks);
    };

    rig.tick(10);                       expectFrames(10);
    rig.pressAndAck();                  expectFrames(12);   // arming + active
    rig.tick(10);                       expectFrames(22);
    rig.router->stopPtt(); rig.tick(5); expectFrames(27);
    rig.router->setMode(RoutingMode::Mute);      rig.tick(5); expectFrames(32);
    rig.router->setMode(RoutingMode::PhoneOnly); rig.tick(5); expectFrames(37);
    rig.router->setMode(RoutingMode::LocalOnly); rig.tick(5); expectFrames(42);
    rig.local->failFromNow();           rig.tick(5); expectFrames(47);  // error path
    CHECK_EQ(rig.router->stats().framesProduced, 47u);
}

SM_TEST(A6, "source switching introduces no discontinuity in the output") {
    Rig rig;
    rig.tick(50);
    rig.pressAndAck();          // switch happens around frame 51
    rig.tick(50);
    rig.router->stopPtt();
    rig.tick(50);

    const auto& s = rig.sink->samples();
    const double localSteady = maxStep(s, 5 * kFrameSamples, 45 * kFrameSamples);
    const double phoneSteady = maxStep(s, 70 * kFrameSamples, 100 * kFrameSamples);
    const double switchIn    = maxStep(s, 50 * kFrameSamples, 56 * kFrameSamples);
    const double switchOut   = maxStep(s, 101 * kFrameSamples, 107 * kFrameSamples);

    const double reference = std::max(localSteady, phoneSteady);
    CHECK(reference > 0.0);
    // A hard cut between two uncorrelated tones would show a step of order the
    // full amplitude -- many times the steady-state per-sample slope.
    CHECK(switchIn  <= reference * 1.15);
    CHECK(switchOut <= reference * 1.15);
}

SM_TEST(A6c, "negative control: the continuity check does catch a hard cut") {
    // Without this, A6 could quietly stop testing anything -- an earlier draft
    // used harmonically related tones and passed with the crossfade disabled,
    // because both sines crossed zero at the switch instant.
    RouterConfig cut;
    cut.crossfadeMs = 0;                     // degenerates to a one-sample jump
    Rig rig(317.0f, 1093.0f, 0.7f, cut);
    rig.tick(50);
    rig.pressAndAck();
    rig.tick(50);

    const auto& s = rig.sink->samples();
    const double reference = std::max(maxStep(s, 5 * kFrameSamples, 45 * kFrameSamples),
                                      maxStep(s, 70 * kFrameSamples, 100 * kFrameSamples));
    const double switchIn = maxStep(s, 50 * kFrameSamples, 56 * kFrameSamples);
    CHECK(switchIn > reference * 2.0);       // the glitch A6 exists to prevent
}

SM_TEST(A6b, "the switch actually changes which source is audible") {
    Rig rig;
    rig.tick(40);
    const size_t beforeEnd = rig.sink->samples().size();
    rig.pressAndAck();
    rig.tick(40);

    const auto& s = rig.sink->samples();
    auto zeroCrossRate = [&](size_t from, size_t to) {
        size_t x = 0;
        for (size_t i = from + 1; i < to; ++i)
            if ((s[i - 1] <= 0.0f) != (s[i] <= 0.0f)) ++x;
        return static_cast<double>(x) / static_cast<double>(to - from);
    };
    const double before = zeroCrossRate(kFrameSamples, beforeEnd - kFrameSamples);
    const double after  = zeroCrossRate(beforeEnd + 5 * kFrameSamples, s.size() - kFrameSamples);
    CHECK(after > before * 2.0);   // 1093 Hz vs 317 Hz
}

SM_TEST(A10, "a dead phone never leaves the router in PHONE_ACTIVE") {
    RouterConfig cfg;
    cfg.phoneLostTimeoutMs = 600;
    Rig rig(317.0f, 1093.0f, 0.7f, cfg);

    rig.pressAndAck();
    CHECK(rig.router->state() == SourceState::PhoneActive);

    // The phone vanishes: no heartbeat, no control message, nothing.
    const size_t budget = 600 / kFrameMillis + 3;   // timeout + fade + closure
    size_t ticks = 0;
    while (rig.router->state() != SourceState::LocalActive && ticks < budget) {
        rig.router->tick();
        ++ticks;
    }
    CHECK(rig.router->state() == SourceState::LocalActive);
    CHECK_EQ(rig.router->stats().phoneLostEvents, 1u);
    CHECK(ticks <= budget);
}

SM_TEST(A10b, "heartbeats hold PHONE_ACTIVE open indefinitely") {
    Rig rig;
    rig.pressAndAck();
    for (int i = 0; i < 500; ++i) {          // 10 seconds of audio
        rig.router->notifyPhoneHeartbeat();
        rig.router->tick();
    }
    CHECK(rig.router->state() == SourceState::PhoneActive);
    CHECK_EQ(rig.router->stats().phoneLostEvents, 0u);
}

SM_TEST(A10c, "arming times out instead of waiting forever") {
    RouterConfig cfg;
    cfg.armTimeoutMs = 200;
    Rig rig(317.0f, 1093.0f, 0.7f, cfg);

    rig.router->startPtt();
    rig.tick(1);
    CHECK(rig.router->state() == SourceState::PhoneArming);

    rig.tick(200 / kFrameMillis + 3);        // phone never becomes ready
    CHECK(rig.router->state() == SourceState::LocalActive);
    CHECK_EQ(rig.router->stats().armTimeouts, 1u);
}

SM_TEST(A11b, "duplicate and out-of-order stops are harmless at the router") {
    Rig rig;
    rig.pressAndAck();
    rig.router->stopPtt();
    rig.router->stopPtt();
    rig.router->stopPtt();
    rig.tick(4);
    CHECK(rig.router->state() == SourceState::LocalActive);

    rig.router->stopPtt();                    // stop with nothing to stop
    rig.tick(1);
    CHECK(rig.router->state() == SourceState::LocalActive);
    CHECK_EQ(rig.router->stats().sinkFailures, 0u);
}

SM_TEST(A13, "a failing source degrades to silence, and the stream keeps flowing") {
    Rig rig;
    rig.tick(10);
    rig.local->failFromNow();
    rig.tick(10);

    const auto st = rig.router->stats();
    CHECK(st.sourceFailures > 0u);
    CHECK_EQ(st.framesProduced, 20u);
    CHECK_EQ(rig.sink->samples().size(), 20u * kFrameSamples);
    // Output during the failure is silence, never stale or garbage audio.
    const auto& s = rig.sink->samples();
    for (size_t i = 15 * kFrameSamples; i < s.size(); ++i) CHECK_NEAR(s[i], 0.0f, 1e-6);
}

SM_TEST(A13b, "a failing sink does not stall the state machine") {
    Rig rig;
    rig.sink->failFromNow();
    rig.pressAndAck();
    rig.tick(10);
    CHECK(rig.router->state() == SourceState::PhoneActive);
    CHECK(rig.router->stats().sinkFailures > 0u);
    CHECK_EQ(rig.router->stats().framesProduced, 12u);
}

SM_TEST(A15, "full-scale sources never produce out-of-range output") {
    Rig rig(317.0f, 1093.0f, 1.0f);
    rig.tick(20);
    rig.pressAndAck();
    rig.tick(20);
    rig.router->stopPtt();
    rig.tick(20);
    for (float v : rig.sink->samples()) CHECK(std::fabs(v) <= 1.0f);
}

SM_TEST(A15c, "entering and leaving Mute does not step the output") {
    Rig rig;
    rig.tick(20);
    rig.router->setMode(RoutingMode::Mute);
    rig.tick(20);
    rig.router->setMode(RoutingMode::Auto);
    rig.tick(20);

    const auto& s = rig.sink->samples();
    const double reference = maxStep(s, 2 * kFrameSamples, 18 * kFrameSamples);
    CHECK(reference > 0.0);
    CHECK(maxStep(s, 20 * kFrameSamples, 24 * kFrameSamples) <= reference * 1.15);  // mute
    CHECK(maxStep(s, 40 * kFrameSamples, 44 * kFrameSamples) <= reference * 1.15);  // unmute
}

SM_TEST(A15b, "mute is silent and local-only ignores the phone entirely") {
    Rig rig;
    rig.router->setMode(RoutingMode::Mute);
    rig.tick(5);
    // The gate ramps rather than steps (the Phase 1 demo showed a boolean gate
    // produces a ~0.4 full-scale click), so silence is asserted after the ramp.
    const auto& muted = rig.sink->samples();
    for (size_t i = kFrameSamples; i < muted.size(); ++i) CHECK_NEAR(muted[i], 0.0f, 1e-6);

    rig.router->setMode(RoutingMode::LocalOnly);
    rig.router->startPtt();
    rig.router->notifyPhoneReady();
    rig.tick(10);
    CHECK(rig.router->state() == SourceState::LocalOnly);
}
