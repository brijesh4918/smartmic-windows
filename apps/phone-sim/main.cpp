// smartmic-phone -- a scriptable stand-in for the mobile app.
//
// It speaks the real protocol, runs the real pairing exchange, encodes real
// Opus and drives the real PTT state model. What it does not have is a UI or a
// platform microphone; the Android and iOS apps replace exactly those two
// pieces behind AudioCaptureEngine / ConnectionEngine (ADR-009).
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "smartmic/audio_format.h"
#include "smartmic/media/phone_media_source.h"
#include "smartmic/media/udp_transport.h"
#include "smartmic/session/peer_session.h"

using namespace smartmic;

namespace {

// The phone's own PTT view (docs/architecture/06-mobile-state-model.md).
enum class PttState { Ready, Connecting, Live, Releasing, Failed };
const char* toStr(PttState s) {
    switch (s) {
        case PttState::Ready:      return "READY";
        case PttState::Connecting: return "CONNECTING";
        case PttState::Live:       return "LIVE";
        case PttState::Releasing:  return "RELEASING";
        case PttState::Failed:     return "FAILED";
    }
    return "?";
}

struct Options {
    std::string uri;
    std::string host = "127.0.0.1";
    uint16_t port = 47820;
    std::string sessionId;
    std::string code;
    double holdAfter = 1.0;
    double holdFor = 2.0;
    double gapFor = 1.0;
    int presses = 1;
    double duration = 6.0;
    bool dieDuringPtt = false;
    double toneHz = 1093.0;
};

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&]() { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
        if (k == "--uri") opt.uri = next();
        else if (k == "--host") opt.host = next();
        else if (k == "--port") opt.port = static_cast<uint16_t>(std::stoi(next()));
        else if (k == "--session") opt.sessionId = next();
        else if (k == "--code") opt.code = next();
        else if (k == "--hold-after") opt.holdAfter = std::stod(next());
        else if (k == "--hold-for") opt.holdFor = std::stod(next());
        else if (k == "--duration") opt.duration = std::stod(next());
        else if (k == "--gap") opt.gapFor = std::stod(next());
        else if (k == "--presses") opt.presses = std::stoi(next());
        else if (k == "--tone") opt.toneHz = std::stod(next());
        else if (k == "--die-during-ptt") opt.dieDuringPtt = true;
    }

    if (!opt.uri.empty()) {
        const auto parsed = security::parsePairingUri(opt.uri);
        if (!parsed) { std::fprintf(stderr, "bad pairing URI\n"); return 2; }
        opt.host = parsed->host;
        opt.port = parsed->port;
        opt.sessionId = parsed->sessionId;
        opt.code = parsed->code;
        std::printf("scanned QR for PC %s (fingerprint %s)\n",
                    parsed->pcDeviceId.c_str(), parsed->fingerprint.c_str());
    }
    if (opt.sessionId.empty() || opt.code.empty()) {
        std::fprintf(stderr, "need --uri, or --session and --code\n");
        return 2;
    }

    security::initCrypto();
    const auto identity = security::DeviceIdentity::generate();

    auto transport = std::make_shared<media::UdpTransport>("phone", 0);
    if (!transport->start() || !transport->setPeer(opt.host, opt.port)) {
        std::fprintf(stderr, "cannot reach %s:%u\n", opt.host.c_str(), opt.port);
        return 1;
    }

    session::PeerSession peer(identity, security::PairingRole::Initiator, transport);
    std::atomic<bool> authenticated{false};
    std::atomic<bool> failed{false};
    std::atomic<PttState> ptt{PttState::Ready};

    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&t0] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    session::SessionCallbacks cb;
    cb.onStateChanged = [&](session::SessionState s) {
        if (s == session::SessionState::Authenticated) authenticated.store(true);
        if (s == session::SessionState::Failed) failed.store(true);
    };
    cb.onControl = [&](const protocol::Message& m) {
        using protocol::MsgType;
        if (m.type == MsgType::PttReady) {
            // The one rule that matters: LIVE only on the PC's acknowledgement.
            ptt.store(PttState::Live);
            std::printf("  [%6.2fs] PTT_READY  -> LIVE   (haptic)\n", elapsed());
        } else if (m.type == MsgType::PttStopped) {
            ptt.store(PttState::Ready);
            std::printf("  [%6.2fs] PTT_STOPPED -> READY (haptic)\n", elapsed());
        }
        std::fflush(stdout);
    };
    peer.setCallbacks(cb);

    std::printf("SmartMic phone (simulated)\n");
    std::printf("  device %s\n", identity.deviceId().c_str());
    std::printf("  pairing with %s:%u, session %s\n\n", opt.host.c_str(), opt.port,
                opt.sessionId.c_str());
    peer.beginPairing(opt.sessionId, opt.code);

    using clock = std::chrono::steady_clock;
    const auto start = t0;
    auto waitUntil = [&](double seconds) {
        std::this_thread::sleep_until(start + std::chrono::duration<double>(seconds));
    };

    // Pairing is a round trip or two; give it a bounded window rather than
    // waiting forever, so a wrong code fails visibly instead of hanging.
    for (int i = 0; i < 100 && !authenticated.load() && !failed.load(); ++i) {
        waitUntil(0.05 * (i + 1));
        if (i % 6 == 5) peer.resendPairingHello();   // ~300 ms, bounded by the loop
    }
    if (!authenticated.load()) {
        std::fprintf(stderr, "pairing failed (%s)\n",
                     failed.load() ? "refused by the PC" : "timed out");
        return 1;
    }
    std::printf("  paired. PC fingerprint %s\n\n", peer.peerFingerprint().c_str());

    media::MediaSender sender;
    sender.open();

    bool pressed = false;
    bool dead = false;
    int pressesDone = 0;
    double phase = 0.0;
    std::vector<float> pcm(kFrameSamples);
    const double inc = 2.0 * M_PI * opt.toneHz / kSampleRate;

    const auto period = std::chrono::milliseconds(kFrameMillis);
    auto nextTick = clock::now();
    while (true) {
        const double t = std::chrono::duration<double>(clock::now() - start).count();
        if (t >= opt.duration) break;

        const double cycleStart = opt.holdAfter + pressesDone * (opt.holdFor + opt.gapFor);
        if (!pressed && pressesDone < opt.presses && t >= cycleStart) {
            pressed = true;
            ptt.store(PttState::Connecting);
            sender.resetStream();
            peer.sendControl(protocol::Message::make(protocol::MsgType::StartPtt, opt.sessionId,
                                                     identity.deviceId(), peer.nextSeq()));
            std::printf("  [%6.2fs] finger down -> CONNECTING\n", t);
            std::fflush(stdout);
        }
        if (pressed && !dead && opt.dieDuringPtt && t >= cycleStart + opt.holdFor * 0.5) {
            // Wi-Fi gone / app killed / battery flat. No STOP_PTT is sent,
            // because a dead phone cannot send one.
            dead = true;
            ++pressesDone;
            std::printf("  [%6.2fs] *** phone dies (no STOP_PTT sent) ***\n", t);
            std::fflush(stdout);
        }
        if (pressed && !dead && t >= cycleStart + opt.holdFor) {
            pressed = false;
            ++pressesDone;
            ptt.store(PttState::Releasing);
            peer.sendControl(protocol::Message::make(protocol::MsgType::StopPtt, opt.sessionId,
                                                     identity.deviceId(), peer.nextSeq(),
                                                     {{"reason", "release"}}));
            std::printf("  [%6.2fs] finger up -> RELEASING\n", t);
            std::fflush(stdout);
        }

        if (pressed && !dead) {
            for (auto& v : pcm) { v = 0.5f * static_cast<float>(std::sin(phase)); phase += inc; }
            std::vector<uint8_t> payload;
            if (sender.packFrame(pcm.data(), pcm.size(), true, payload)) {
                peer.sendMedia(payload.data(), payload.size());
            }
        }

        nextTick += period;
        std::this_thread::sleep_until(nextTick);
    }

    std::printf("\n  final PTT state: %s\n", toStr(ptt.load()));
    transport->stop();
    return 0;
}
