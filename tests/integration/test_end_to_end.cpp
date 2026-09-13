// End-to-end: simulated phone -> Opus -> AEAD -> lossy network -> jitter buffer
// -> AudioRouter -> sink, driven by the real state machine and the real
// watchdog.
//
// Single-threaded and deterministic on purpose: every failure here is
// reproducible from its seed, which is what makes the chaos runs worth trusting.
#include <cmath>
#include <memory>
#include <vector>

#include "host_sources.h"
#include "smartmic/media/chaos_transport.h"
#include "smartmic/media/phone_media_source.h"
#include "smartmic/media/secure_channel.h"
#include "smartmic/media/transport.h"
#include "smartmic/router.h"
#include "smartmic/security/pairing.h"
#include "test_harness.h"

using namespace smartmic;
using namespace smartmic::media;
using namespace smartmic::security;

namespace {

// Everything except WASAPI, the kernel driver and the mobile UI.
class EndToEndRig {
public:
    explicit EndToEndRig(ChaosConfig chaos = {}, RouterConfig routerCfg = {}) {
        // --- pairing: derive a real session key from a real exchange -------
        const auto pcId = DeviceIdentity::generate();
        const auto phoneId = DeviceIdentity::generate();
        PairingExchange responder(pcId, PairingRole::Responder, "e2e-session", "314159");
        PairingExchange initiator(phoneId, PairingRole::Initiator, "e2e-session", "314159");
        responder.onPeer(initiator.publicKey(), initiator.share());
        initiator.onPeer(responder.publicKey(), responder.share());
        paired_ = responder.verifyPeerAuth(initiator.authSignature()) == PairingError::None &&
                  initiator.verifyPeerAuth(responder.authSignature()) == PairingError::None;
        sessionKey_ = responder.sessionKey();

        phoneChannel_.init(initiator.sessionKey(), Direction::PhoneToPc);
        pcChannel_.init(sessionKey_, Direction::PcToPhone);

        // --- transport ------------------------------------------------------
        phoneTransport_ = std::make_shared<LoopbackTransport>("phone");
        pcTransport_ = std::make_shared<LoopbackTransport>("pc");
        phoneTransport_->connectTo(pcTransport_.get());
        pcTransport_->connectTo(phoneTransport_.get());
        phoneTransport_->start();
        pcTransport_->start();
        network_ = std::make_shared<ChaosTransport>(phoneTransport_, chaos);
        network_->start();

        // --- PC side --------------------------------------------------------
        phoneSource_ = std::make_shared<PhoneMediaSource>("phone");
        phoneSource_->start();
        localSource_ = std::make_shared<host::ToneSource>("local", 317.0f, 0.5f);
        localSource_->start();
        sink_ = std::make_shared<host::MemorySink>("sink");
        sink_->start();

        pcTransport_->onPacket([this](const uint8_t* data, size_t len) {
            std::vector<uint8_t> plain;
            if (!pcChannel_.open(data, len, plain)) {
                ++rejectedPackets_;
                return;   // forged, replayed or corrupt: it never reaches the router
            }
            phoneSource_->onMediaPayload(plain.data(), plain.size(), nowMs_);
        });

        router_ = std::make_unique<AudioRouter>(routerCfg, &registry_);
        router_->setLocalSource(localSource_);
        router_->setPhoneSource(phoneSource_);
        router_->setSink(sink_);

        // --- phone side -----------------------------------------------------
        sender_.open();
    }

    bool paired() const { return paired_; }

    // One 20 ms step of the whole system.
    void step(bool phoneTransmitting) {
        if (phoneTransmitting && phoneAlive_) {
            const auto pcm = phoneTone(1093.0, kFrameSamples);
            std::vector<uint8_t> payload, sealed;
            if (sender_.packFrame(pcm.data(), pcm.size(), true, payload) &&
                phoneChannel_.seal(payload.data(), payload.size(), sealed)) {
                network_->send(sealed.data(), sealed.size());
            }
        }
        // Liveness is driven by media arrival, exactly as the service does it:
        // a heartbeat the phone cannot actually deliver is not liveness.
        if (phoneAlive_ && phoneTransmitting) router_->notifyPhoneHeartbeat();

        if (!armed_ && phoneSource_->ready() && router_->state() == SourceState::PhoneArming) {
            router_->notifyPhoneReady();   // the service's PTT_READY trigger
            armed_ = true;
        }
        router_->tick();
        nowMs_ += kFrameMillis;
    }

    void steps(int n, bool transmitting) { for (int i = 0; i < n; ++i) step(transmitting); }

    void pressPtt() { armed_ = false; sender_.resetStream(); router_->startPtt(); }
    void releasePtt() { router_->stopPtt(); }
    void killPhone() { phoneAlive_ = false; }
    void revivePhone() { phoneAlive_ = true; sender_.resetStream(); }

    AudioRouter& router() { return *router_; }
    host::MemorySink& sink() { return *sink_; }
    PhoneMediaSource& phoneSource() { return *phoneSource_; }
    ChaosTransport& network() { return *network_; }
    uint64_t rejectedPackets() const { return rejectedPackets_; }
    SecureChannel& pcChannel() { return pcChannel_; }
    uint64_t nowMs() const { return nowMs_; }

    // Injects a packet that did not come from the paired phone.
    void injectForged(const std::vector<uint8_t>& bytes) {
        pcTransport_->deliver(bytes.data(), bytes.size());
    }

private:
    std::vector<float> phoneTone(double hz, size_t n) {
        std::vector<float> v(n);
        const double inc = 2.0 * M_PI * hz / kSampleRate;
        for (size_t i = 0; i < n; ++i) {
            v[i] = 0.5f * static_cast<float>(std::sin(phonePhase_));
            phonePhase_ += inc;
        }
        return v;
    }

    bool paired_ = false;
    SessionKey sessionKey_{};
    SecureChannel phoneChannel_, pcChannel_;
    std::shared_ptr<LoopbackTransport> phoneTransport_, pcTransport_;
    std::shared_ptr<ChaosTransport> network_;
    MediaSender sender_;
    double phonePhase_ = 0.0;
    bool phoneAlive_ = true;
    bool armed_ = false;

    DeviceRegistry registry_;
    std::shared_ptr<PhoneMediaSource> phoneSource_;
    std::shared_ptr<host::ToneSource> localSource_;
    std::shared_ptr<host::MemorySink> sink_;
    std::unique_ptr<AudioRouter> router_;
    uint64_t rejectedPackets_ = 0;
    uint64_t nowMs_ = 1000;
};

double rmsOf(const std::vector<float>& s, size_t from, size_t to) {
    to = std::min(to, s.size());
    if (to <= from) return 0.0;
    double acc = 0.0;
    for (size_t i = from; i < to; ++i) acc += static_cast<double>(s[i]) * s[i];
    return std::sqrt(acc / static_cast<double>(to - from));
}

double dominantHz(const std::vector<float>& s, size_t from, size_t to) {
    to = std::min(to, s.size());
    size_t crossings = 0;
    for (size_t i = from + 1; i < to; ++i) {
        if ((s[i - 1] <= 0.0f) != (s[i] <= 0.0f)) ++crossings;
    }
    const double seconds = static_cast<double>(to - from) / kSampleRate;
    return seconds > 0 ? (static_cast<double>(crossings) / 2.0) / seconds : 0.0;
}

}  // namespace

SM_TEST(E1, "the phone's voice reaches the sink, end to end, over a real pipeline") {
    EndToEndRig rig;
    CHECK(rig.paired());

    rig.steps(50, false);                   // local microphone only
    const size_t localEnd = rig.sink().samples().size();

    rig.pressPtt();
    rig.steps(50, true);                    // PTT held

    const auto& s = rig.sink().samples();
    CHECK(rig.router().state() == SourceState::PhoneActive);

    // Before: 317 Hz local tone. After: 1093 Hz phone tone, through Opus,
    // encryption, the network and the jitter buffer.
    CHECK_NEAR(dominantHz(s, 5 * kFrameSamples, localEnd - kFrameSamples), 317.0, 20.0);
    CHECK_NEAR(dominantHz(s, localEnd + 20 * kFrameSamples, s.size()), 1093.0, 60.0);
    CHECK(rmsOf(s, localEnd + 20 * kFrameSamples, s.size()) > 0.2);

    // Note: `s` aliases the sink's live buffer, so the boundary has to be
    // captured before more audio is appended, not read back from s.size().
    const size_t phoneEnd = rig.sink().samples().size();
    rig.releasePtt();
    rig.steps(20, false);
    CHECK(rig.router().state() == SourceState::LocalActive);
    CHECK_NEAR(dominantHz(rig.sink().samples(), phoneEnd + 5 * kFrameSamples,
                          rig.sink().samples().size()), 317.0, 20.0);
}

SM_TEST(E2, "LIVE is never entered before the PC actually has phone audio") {
    // ADR-007's central rule, checked at the layer that decides it.
    EndToEndRig rig;
    rig.steps(20, false);
    rig.pressPtt();
    rig.steps(1, false);   // pressed, but the phone has not sent anything yet

    CHECK(rig.router().state() == SourceState::PhoneArming);
    CHECK(!rig.phoneSource().ready());

    // The phone starts transmitting; the router only goes active once the
    // jitter buffer has real, decodable audio in it.
    int ticks = 0;
    while (rig.router().state() != SourceState::PhoneActive && ticks < 50) {
        rig.step(true);
        ++ticks;
    }
    CHECK(rig.router().state() == SourceState::PhoneActive);
    CHECK(ticks >= 2);     // never instantaneous: the prefill has to happen
    // ready() is the arming gate, not a steady-state property: once active the
    // router consumes a frame per tick, so depth sits near the target and dips
    // below the prefill mark. Asserting it here would be asserting the wrong
    // thing -- what matters is that arming waited for it.
}

SM_TEST(E3, "speech survives 5 percent packet loss") {
    ChaosConfig chaos;
    chaos.lossRate = 0.05;
    chaos.seed = 4242;
    EndToEndRig rig(chaos);

    rig.steps(20, false);
    rig.pressPtt();
    rig.steps(200, true);

    CHECK(rig.router().state() == SourceState::PhoneActive);
    const auto& s = rig.sink().samples();
    const size_t from = s.size() - 150 * kFrameSamples;
    // Opus PLC fills the gaps; the level must not collapse.
    CHECK(rmsOf(s, from, s.size()) > 0.2);
    CHECK_NEAR(dominantHz(s, from, s.size()), 1093.0, 80.0);

    const auto js = rig.phoneSource().jitterStats();
    CHECK(js.concealed > 0u);                      // loss really happened...
    CHECK(js.concealed < js.popped / 4);           // ...and was mostly absorbed
}

SM_TEST(E4, "speech survives burst loss and reordering") {
    ChaosConfig chaos;
    chaos.lossRate = 0.03;
    chaos.burstLossLength = 3;       // 60 ms gone at a time
    chaos.reorderRate = 0.05;
    chaos.duplicateRate = 0.02;
    chaos.seed = 777;
    EndToEndRig rig(chaos);

    rig.steps(20, false);
    rig.pressPtt();
    rig.steps(300, true);

    CHECK(rig.router().state() == SourceState::PhoneActive);
    const auto& s = rig.sink().samples();
    CHECK(rmsOf(s, s.size() - 200 * kFrameSamples, s.size()) > 0.15);
    const auto js = rig.phoneSource().jitterStats();
    // A duplicate is counted as such if its twin is still queued, and as
    // "too late" if the original has already been played out. Either way it
    // must not have been played twice.
    CHECK(js.duplicates + js.tooLate > 0u);
}

SM_TEST(E5, "a phone that dies mid-PTT never leaves the microphone stuck") {
    RouterConfig cfg;
    cfg.phoneLostTimeoutMs = 600;
    EndToEndRig rig({}, cfg);

    rig.steps(20, false);
    rig.pressPtt();
    rig.steps(50, true);
    CHECK(rig.router().state() == SourceState::PhoneActive);

    rig.killPhone();                 // Wi-Fi gone, app killed, battery flat
    const size_t budget = 600 / kFrameMillis + 4;
    size_t ticks = 0;
    while (rig.router().state() != SourceState::LocalActive && ticks < budget) {
        rig.step(true);              // the phone "tries" to send; nothing arrives
        ++ticks;
    }
    CHECK(rig.router().state() == SourceState::LocalActive);
    CHECK(ticks <= budget);

    // Let the return settle, then confirm the local microphone is genuinely
    // back -- measuring inside the fade would just be measuring the fade.
    rig.steps(15, false);
    const auto& s = rig.sink().samples();
    CHECK_NEAR(dominantHz(s, s.size() - 10 * kFrameSamples, s.size()), 317.0, 25.0);
    CHECK(rmsOf(s, s.size() - 10 * kFrameSamples, s.size()) > 0.2);
}

SM_TEST(E6, "two hundred induced failures never leave the router in PHONE_ACTIVE") {
    // The Phase 7 chaos gate, run at Phase 3 so the property is protected from
    // the moment it exists rather than asserted at the end.
    for (int trial = 0; trial < 200; ++trial) {
        ChaosConfig chaos;
        chaos.lossRate = (trial % 5) * 0.05;      // 0 .. 20 %
        chaos.burstLossLength = (trial % 3) + 1;
        chaos.reorderRate = (trial % 7) * 0.01;
        chaos.seed = static_cast<uint64_t>(trial) * 7919 + 13;
        EndToEndRig rig(chaos);

        rig.steps(5, false);
        rig.pressPtt();
        rig.steps(10 + (trial % 20), true);
        rig.killPhone();
        rig.steps(600 / kFrameMillis + 6, true);

        if (rig.router().state() != SourceState::LocalActive) {
            ::smartmic::test::fail(__FILE__, __LINE__,
                                   "stuck in " + std::string(toString(rig.router().state())) +
                                       " on trial " + std::to_string(trial) +
                                       " (seed " + std::to_string(chaos.seed) + ")");
        }
    }
}

SM_TEST(E7, "unauthenticated audio can never reach the router") {
    // Threat model T1, asserted at the router rather than at the socket.
    EndToEndRig rig;
    rig.steps(20, false);
    rig.pressPtt();
    rig.steps(30, true);
    const auto before = rig.phoneSource().jitterStats().pushed;

    // An attacker on the LAN who knows the wire format exactly, but not the key.
    SecureChannel attacker;
    SessionKey wrong{};
    wrong.fill(0x99);
    attacker.init(wrong, Direction::PhoneToPc);

    MediaSender evil;
    evil.open();
    double phase = 0.0;
    for (int i = 0; i < 100; ++i) {
        std::vector<float> pcm(kFrameSamples);
        for (auto& v : pcm) { v = 0.9f * static_cast<float>(std::sin(phase)); phase += 0.05; }
        std::vector<uint8_t> payload, sealed;
        evil.packFrame(pcm.data(), pcm.size(), true, payload);
        attacker.seal(payload.data(), payload.size(), sealed);
        rig.injectForged(sealed);
    }
    // Random garbage too.
    for (int i = 0; i < 100; ++i) rig.injectForged(std::vector<uint8_t>(60, static_cast<uint8_t>(i)));

    CHECK_EQ(rig.phoneSource().jitterStats().pushed, before);   // not one frame got in
    CHECK_EQ(rig.rejectedPackets(), 200u);
    CHECK(rig.pcChannel().authFailures() > 0u);
}

SM_TEST(E8, "a press, release, press cycle works repeatedly without drift") {
    EndToEndRig rig;
    rig.steps(10, false);
    for (int cycle = 0; cycle < 10; ++cycle) {
        rig.pressPtt();
        rig.steps(15, true);
        CHECK(rig.router().state() == SourceState::PhoneActive);
        rig.releasePtt();
        rig.steps(10, false);
        CHECK(rig.router().state() == SourceState::LocalActive);
    }
    const auto st = rig.router().stats();
    CHECK_EQ(st.sinkFailures, 0u);
    CHECK_EQ(st.armTimeouts, 0u);
    CHECK_EQ(st.framesProduced, rig.sink().samples().size() / kFrameSamples);
}
