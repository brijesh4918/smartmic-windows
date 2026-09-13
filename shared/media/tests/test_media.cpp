#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

#include "smartmic/media/chaos_transport.h"
#include "smartmic/media/jitter_buffer.h"
#include "smartmic/media/opus_codec.h"
#include "smartmic/media/phone_media_source.h"
#include "smartmic/media/secure_channel.h"
#include "smartmic/media/transport.h"
#include "smartmic/security/pairing.h"
#include "test_harness.h"

using namespace smartmic;
using namespace smartmic::media;

namespace {

std::vector<float> tone(double hz, size_t samples, double& phase, float amp = 0.5f) {
    std::vector<float> v(samples);
    const double inc = 2.0 * M_PI * hz / kSampleRate;
    for (size_t i = 0; i < samples; ++i) {
        v[i] = amp * static_cast<float>(std::sin(phase));
        phase += inc;
    }
    return v;
}

double rms(const float* v, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(v[i]) * v[i];
    return std::sqrt(acc / n);
}

security::SessionKey testKey(uint8_t fill) {
    security::SessionKey k{};
    k.fill(fill);
    return k;
}

}  // namespace

// --- codec -----------------------------------------------------------------

SM_TEST(C1, "Opus encodes and decodes a canonical frame at a sane bitrate") {
    OpusEncoderWrapper enc;
    OpusDecoderWrapper dec;
    CHECK(enc.open());
    CHECK(dec.open());

    double phase = 0.0;
    std::vector<float> out(kFrameSamples);
    size_t totalBytes = 0;
    double lastRms = 0.0;

    // Opus needs a few frames to converge; judge the steady state, not frame 0.
    for (int i = 0; i < 50; ++i) {
        const auto pcm = tone(440.0, kFrameSamples, phase);
        std::vector<uint8_t> packet;
        CHECK(enc.encode(pcm.data(), pcm.size(), packet));
        CHECK(!packet.empty());
        totalBytes += packet.size();
        CHECK(dec.decode(packet.data(), packet.size(), out.data(), out.size()));
        lastRms = rms(out.data(), out.size());
    }

    // 28 kbps at 20 ms frames is ~70 bytes per packet.
    const double avgBytes = static_cast<double>(totalBytes) / 50.0;
    CHECK(avgBytes > 20.0 && avgBytes < 200.0);
    // A 0.5-amplitude sine has RMS ~0.354; lossy coding should stay close.
    CHECK_NEAR(lastRms, 0.354, 0.08);
}

SM_TEST(C2, "packet-loss concealment produces audio, not silence") {
    OpusEncoderWrapper enc;
    OpusDecoderWrapper dec;
    CHECK(enc.open());
    CHECK(dec.open());

    double phase = 0.0;
    std::vector<float> out(kFrameSamples);
    for (int i = 0; i < 20; ++i) {
        const auto pcm = tone(440.0, kFrameSamples, phase);
        std::vector<uint8_t> packet;
        enc.encode(pcm.data(), pcm.size(), packet);
        dec.decode(packet.data(), packet.size(), out.data(), out.size());
    }
    CHECK(dec.conceal(out.data(), out.size()));
    // PLC extrapolates the waveform; substituting silence would read as ~0.
    CHECK(rms(out.data(), out.size()) > 0.05);
}

SM_TEST(C3, "a corrupt packet is concealed rather than decoded into garbage") {
    OpusEncoderWrapper enc;
    OpusDecoderWrapper dec;
    enc.open();
    dec.open();
    double phase = 0.0;
    const auto pcm = tone(440.0, kFrameSamples, phase);
    std::vector<uint8_t> packet;
    enc.encode(pcm.data(), pcm.size(), packet);

    std::vector<uint8_t> corrupt(packet.size(), 0xFF);
    std::vector<float> out(kFrameSamples, 99.0f);
    dec.decode(corrupt.data(), corrupt.size(), out.data(), out.size());
    for (float v : out) CHECK(std::fabs(v) <= 1.0f);   // never garbage
}

// --- secure channel --------------------------------------------------------

SM_TEST(C4, "sealed packets round-trip between the two directions") {
    const auto key = testKey(0x42);
    SecureChannel phone, pc;
    CHECK(phone.init(key, Direction::PhoneToPc));
    CHECK(pc.init(key, Direction::PcToPhone));

    const std::vector<uint8_t> plain{'a', 'u', 'd', 'i', 'o', 0, 1, 2, 3};
    std::vector<uint8_t> sealed, opened;
    CHECK(phone.seal(plain.data(), plain.size(), sealed));
    CHECK(sealed.size() == plain.size() + kSecureOverhead);
    // The plaintext must not be recoverable by inspection (threat model T4).
    CHECK(std::search(sealed.begin(), sealed.end(), plain.begin(), plain.end()) == sealed.end());

    CHECK(pc.open(sealed.data(), sealed.size(), opened));
    CHECK(opened == plain);
}

SM_TEST(C5, "tampered, truncated and wrong-key packets are all rejected") {
    const auto key = testKey(0x42);
    SecureChannel phone, pc;
    phone.init(key, Direction::PhoneToPc);
    pc.init(key, Direction::PcToPhone);

    const std::vector<uint8_t> plain{'h', 'e', 'l', 'l', 'o'};
    std::vector<uint8_t> sealed, out;
    phone.seal(plain.data(), plain.size(), sealed);

    auto flipped = sealed;
    flipped[flipped.size() - 1] ^= 0x01;
    CHECK(!pc.open(flipped.data(), flipped.size(), out));

    auto shortened = sealed;
    shortened.resize(sealed.size() - 1);
    CHECK(!pc.open(shortened.data(), shortened.size(), out));
    CHECK(!pc.open(sealed.data(), 3, out));

    SecureChannel stranger;
    stranger.init(testKey(0x43), Direction::PcToPhone);
    CHECK(!stranger.open(sealed.data(), sealed.size(), out));

    CHECK_EQ(pc.authFailures(), 2u);     // flipped byte, and the short ciphertext
    CHECK_EQ(pc.malformed(), 1u);        // the 3-byte runt, rejected before any crypto
    CHECK_EQ(pc.rejected(), 3u);
    CHECK(pc.open(sealed.data(), sealed.size(), out));   // the genuine one still works
}

SM_TEST(C6, "a replayed media packet is refused") {
    const auto key = testKey(0x42);
    SecureChannel phone, pc;
    phone.init(key, Direction::PhoneToPc);
    pc.init(key, Direction::PcToPhone);

    std::vector<uint8_t> sealed, out;
    const std::vector<uint8_t> plain{1, 2, 3};
    phone.seal(plain.data(), plain.size(), sealed);

    CHECK(pc.open(sealed.data(), sealed.size(), out));
    for (int i = 0; i < 50; ++i) CHECK(!pc.open(sealed.data(), sealed.size(), out));
    CHECK(pc.replayRejects() == 50u);
}

SM_TEST(C7, "the two directions use different keys, so packets cannot be reflected") {
    const auto key = testKey(0x42);
    SecureChannel phone, pc;
    phone.init(key, Direction::PhoneToPc);
    pc.init(key, Direction::PcToPhone);

    std::vector<uint8_t> sealed, out;
    const std::vector<uint8_t> plain{9, 9, 9};
    phone.seal(plain.data(), plain.size(), sealed);

    // Reflecting the phone's own packet back at it must fail.
    SecureChannel phoneAgain;
    phoneAgain.init(key, Direction::PhoneToPc);
    CHECK(!phoneAgain.open(sealed.data(), sealed.size(), out));
}

// --- jitter buffer ---------------------------------------------------------

namespace {

// Feeds `count` encoded frames into a jitter buffer through an optional
// transform, then drains it, and reports what came out.
struct JitterRun {
    uint64_t realFrames = 0;
    uint64_t concealedFrames = 0;
    JitterStats stats;
};

JitterRun runJitter(JitterConfig cfg, size_t count,
                    const std::function<void(uint32_t seq, const std::vector<uint8_t>&,
                                             JitterBuffer&, uint64_t)>& feed) {
    OpusEncoderWrapper enc;
    OpusDecoderWrapper dec;
    enc.open();
    dec.open();
    JitterBuffer jb(cfg);

    double phase = 0.0;
    std::vector<std::vector<uint8_t>> packets;
    for (size_t i = 0; i < count; ++i) {
        const auto pcm = tone(440.0, kFrameSamples, phase);
        std::vector<uint8_t> p;
        enc.encode(pcm.data(), pcm.size(), p);
        packets.push_back(std::move(p));
    }

    JitterRun r;
    std::vector<float> out(kFrameSamples);
    uint64_t now = 0;
    for (size_t i = 0; i < count; ++i) {
        feed(static_cast<uint32_t>(i), packets[i], jb, now);
        now += kFrameMillis;
        if (jb.ready()) {
            if (jb.pop(dec, out.data(), out.size(), now)) ++r.realFrames;
            else ++r.concealedFrames;
        }
    }
    r.stats = jb.stats();
    return r;
}

}  // namespace

SM_TEST(J1, "a clean stream produces real frames and no concealment") {
    const auto r = runJitter({}, 200, [](uint32_t seq, const std::vector<uint8_t>& p,
                                        JitterBuffer& jb, uint64_t now) {
        jb.push(seq, p.data(), p.size(), now);
    });
    CHECK(r.realFrames > 190u);
    CHECK_EQ(r.concealedFrames, 0u);
    CHECK_EQ(r.stats.duplicates, 0u);
}

SM_TEST(J2, "duplicates are discarded exactly once each") {
    const auto r = runJitter({}, 100, [](uint32_t seq, const std::vector<uint8_t>& p,
                                        JitterBuffer& jb, uint64_t now) {
        jb.push(seq, p.data(), p.size(), now);
        jb.push(seq, p.data(), p.size(), now);   // every packet arrives twice
    });
    CHECK(r.stats.duplicates > 90u);
    CHECK_EQ(r.concealedFrames, 0u);             // duplication must not cause gaps
}

SM_TEST(J3, "reordering inside the buffer is absorbed without concealment") {
    JitterConfig cfg;
    cfg.prefillMs = 60;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> held;
    const auto r = runJitter(cfg, 100, [&held](uint32_t seq, const std::vector<uint8_t>& p,
                                               JitterBuffer& jb, uint64_t now) {
        // Deliver in swapped pairs: 1,0,3,2,5,4...
        held.emplace_back(seq, p);
        if (held.size() == 2) {
            jb.push(held[1].first, held[1].second.data(), held[1].second.size(), now);
            jb.push(held[0].first, held[0].second.data(), held[0].second.size(), now);
            held.clear();
        }
    });
    CHECK(r.stats.reordered > 0u);
    CHECK(r.concealedFrames <= 2u);
}

SM_TEST(J4, "a packet that arrives after its slot was served is counted, not played late") {
    JitterConfig cfg;
    cfg.prefillMs = 20;
    OpusEncoderWrapper enc;
    OpusDecoderWrapper dec;
    enc.open();
    dec.open();
    JitterBuffer jb(cfg);

    double phase = 0.0;
    std::vector<std::vector<uint8_t>> packets;
    for (int i = 0; i < 10; ++i) {
        const auto pcm = tone(440.0, kFrameSamples, phase);
        std::vector<uint8_t> p;
        enc.encode(pcm.data(), pcm.size(), p);
        packets.push_back(p);
    }
    std::vector<float> out(kFrameSamples);
    for (int i = 0; i < 6; ++i) jb.push(static_cast<uint32_t>(i), packets[i].data(), packets[i].size(), i * 20);
    for (int i = 0; i < 5; ++i) jb.pop(dec, out.data(), out.size(), 200);

    jb.push(1, packets[1].data(), packets[1].size(), 300);   // far too late
    CHECK(jb.stats().tooLate >= 1u);
}

SM_TEST(J5, "the adaptive target grows when arrivals get jittery") {
    JitterConfig cfg;
    cfg.adaptive = true;
    cfg.targetMs = 20;
    OpusEncoderWrapper enc;
    enc.open();
    JitterBuffer jb(cfg);
    double phase = 0.0;
    std::vector<uint8_t> p;
    const auto pcm = tone(440.0, kFrameSamples, phase);
    enc.encode(pcm.data(), pcm.size(), p);

    const uint32_t before = jb.stats().targetMs;
    uint64_t now = 0;
    for (uint32_t i = 0; i < 200; ++i) {
        now += (i % 2) ? 5 : 60;      // wildly irregular arrivals
        jb.push(i, p.data(), p.size(), now);
    }
    CHECK(jb.stats().targetMs > before);
    CHECK(jb.stats().targetMs <= cfg.maxMs);
}

SM_TEST(J6, "a stalled stream is reported rather than waited on forever") {
    JitterConfig cfg;
    cfg.stallMs = 300;
    JitterBuffer jb(cfg);
    std::vector<uint8_t> p{1, 2, 3};
    jb.push(0, p.data(), p.size(), 1000);
    CHECK(!jb.stalled(1200));
    CHECK(jb.stalled(1400));
}

// --- chaos transport -------------------------------------------------------

SM_TEST(X1, "the chaos injector drops, duplicates and reorders at the configured rates") {
    auto a = std::make_shared<LoopbackTransport>("a");
    auto b = std::make_shared<LoopbackTransport>("b");
    a->connectTo(b.get());
    a->start();
    b->start();

    size_t received = 0;
    b->onPacket([&](const uint8_t*, size_t) { ++received; });

    ChaosConfig cfg;
    cfg.lossRate = 0.10;
    cfg.duplicateRate = 0.05;
    cfg.seed = 12345;
    ChaosTransport chaos(a, cfg);
    chaos.start();

    const std::vector<uint8_t> packet{1, 2, 3, 4};
    for (int i = 0; i < 2000; ++i) chaos.send(packet.data(), packet.size());
    chaos.flush();

    const auto cs = chaos.chaosStats();
    CHECK_EQ(cs.offered, 2000u);
    CHECK(cs.dropped > 120u && cs.dropped < 280u);       // ~10 %
    CHECK(cs.duplicated > 50u && cs.duplicated < 150u);  // ~5 % of what survives
    CHECK(received == cs.offered - cs.dropped + cs.duplicated);
}

SM_TEST(X2, "chaos is deterministic for a given seed, so failures are reproducible") {
    auto run = [](uint64_t seed) {
        auto a = std::make_shared<LoopbackTransport>("a");
        auto b = std::make_shared<LoopbackTransport>("b");
        a->connectTo(b.get());
        a->start();
        b->start();
        ChaosConfig cfg;
        cfg.lossRate = 0.2;
        cfg.seed = seed;
        ChaosTransport chaos(a, cfg);
        const std::vector<uint8_t> p{7};
        for (int i = 0; i < 500; ++i) chaos.send(p.data(), p.size());
        return chaos.chaosStats().dropped;
    };
    CHECK_EQ(run(99), run(99));
    CHECK(run(99) != run(100));
}
