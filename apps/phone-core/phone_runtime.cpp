/*  phone_runtime.cpp -- the engine behind smartmic_phone_api.h.
 *
 *  Reuses the same protocol, pairing, codec and session code the desktop
 *  service and the test suite use. The only phone-specific parts are the
 *  microphone and this thin C surface.
 */
#include "smartmic_phone_api.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture.h"
#include "smartmic/audio_format.h"
#include "smartmic/media/phone_media_source.h"
#include "smartmic/media/udp_transport.h"
#include "smartmic/session/peer_session.h"

using namespace smartmic;

namespace {

constexpr char kVersion[] = "SmartMic phone core 0.3.0";

constexpr int kArmTimeoutMs = 1500;     // matches the service's arm timeout
constexpr int kReleaseTimeoutMs = 800;  // a lost PTT_STOPPED must not pin the UI

uint64_t nowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

security::DeviceIdentity loadOrCreateIdentity(const std::string& dir) {
    security::initCrypto();
    const std::string path = dir.empty() ? std::string() : dir + "/identity.key";
    if (!path.empty()) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
            if (auto id = security::DeviceIdentity::fromSecretKey(bytes)) return *id;
        }
    }
    const auto id = security::DeviceIdentity::generate();
    if (!path.empty()) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(id.secretKey().data()),
                  (std::streamsize)id.secretKey().size());
    }
    return id;
}

}  // namespace

struct sm_phone {
    /* --- identity and transport --- */
    security::DeviceIdentity identity{security::DeviceIdentity::generate()};
    std::shared_ptr<media::UdpTransport> transport;
    std::unique_ptr<session::PeerSession> peer;
    media::MediaSender sender;
    std::unique_ptr<phone::ICapture> capture;

    std::string host, sessionId, code;
    uint16_t port = 0;

    /* --- state, all read from Dart on the UI thread --- */
    std::atomic<int> connState{SM_CONN_IDLE};
    std::atomic<int> pttState{SM_PTT_READY};
    std::atomic<int> rttMs{-1};
    std::atomic<float> micLevel{0.0f};
    std::atomic<int> haptic{SM_HAPTIC_NONE};

    std::atomic<bool> pressed{false};
    std::atomic<uint64_t> pressedAtMs{0};
    std::atomic<uint64_t> releasedAtMs{0};
    std::atomic<uint64_t> pingSentMs{0};

    std::atomic<bool> running{false};
    std::thread worker;

    std::mutex textMu;
    std::string peerFingerprint, deviceId, lastError, pcStatus;

    /* Scratch buffers owned by the handle so the C API can hand out pointers
       that stay valid until the next call. */
    std::string outFingerprint, outDeviceId, outError, outStatus;

    void setError(std::string e) {
        std::lock_guard<std::mutex> lk(textMu);
        lastError = std::move(e);
    }
};

namespace {

void onControl(sm_phone* p, const protocol::Message& m) {
    using protocol::MsgType;
    switch (m.type) {
    case MsgType::PttReady:
        /* The one rule that matters: LIVE only on the PC's acknowledgement,
           never on local optimism (ADR-007). */
        if (p->pressed.load()) {
            p->pttState.store(SM_PTT_LIVE);
            p->haptic.store(SM_HAPTIC_WENT_LIVE);
        }
        break;

    case MsgType::PttStopped:
        p->pttState.store(SM_PTT_READY);
        p->haptic.store(SM_HAPTIC_RETURNED);
        break;

    case MsgType::Pong: {
        const uint64_t sent = p->pingSentMs.load();
        if (sent != 0) p->rttMs.store((int)(nowMs() - sent));
        break;
    }

    case MsgType::Status: {
        std::lock_guard<std::mutex> lk(p->textMu);
        p->pcStatus = m.payload.dump();
        break;
    }

    case MsgType::Error:
        p->setError(m.payload.value("message", "the PC reported an error"));
        break;

    default:
        break;
    }
}

void onCaptureFrame(sm_phone* p, const float* pcm, size_t samples) {
    /* Level meter runs whether or not we are transmitting, so the UI can show
       that the microphone is alive before the user commits to talking. */
    double acc = 0.0;
    for (size_t i = 0; i < samples; ++i) acc += (double)pcm[i] * pcm[i];
    const float rms = (float)std::sqrt(acc / (double)samples);
    /* Light smoothing: a raw 20 ms RMS makes a meter that twitches. */
    p->micLevel.store(p->micLevel.load() * 0.6f + rms * 0.4f);

    if (!p->pressed.load() || p->connState.load() != SM_CONN_AUTHENTICATED) return;
    if (samples != kFrameSamples) return;

    std::vector<uint8_t> payload;
    if (p->sender.packFrame(pcm, samples, true, payload)) {
        p->peer->sendMedia(payload.data(), payload.size());
    }
}

void workerMain(sm_phone* p) {
    uint64_t lastPairingRetry = 0;
    uint64_t lastPing = 0;

    while (p->running.load()) {
        const uint64_t t = nowMs();

        /* Pairing: UDP has no retransmission and the first datagram is the one
           most likely to be lost, so it is re-sent until the PC answers. */
        if (p->connState.load() == SM_CONN_PAIRING && t - lastPairingRetry > 400) {
            p->peer->resendPairingHello();
            lastPairingRetry = t;
        }

        if (p->connState.load() == SM_CONN_AUTHENTICATED) {
            if (t - lastPing > 1000) {
                p->pingSentMs.store(t);
                p->peer->sendControl(protocol::Message::make(
                    protocol::MsgType::Ping, p->sessionId, p->identity.deviceId(),
                    p->peer->nextSeq(), {{"pttActive", p->pressed.load()}}));
                lastPing = t;
            }

            /* Arming must not hang: if the PC never acknowledges, the UI says
               FAILED rather than sitting on CONNECTING forever. */
            if (p->pttState.load() == SM_PTT_CONNECTING &&
                t - p->pressedAtMs.load() > (uint64_t)kArmTimeoutMs) {
                p->pttState.store(SM_PTT_FAILED);
                p->haptic.store(SM_HAPTIC_FAILED);
                p->pressed.store(false);
                p->setError("the PC did not accept the microphone in time");
            }

            /* Likewise a lost PTT_STOPPED must not pin the UI in RELEASING. */
            if (p->pttState.load() == SM_PTT_RELEASING &&
                t - p->releasedAtMs.load() > (uint64_t)kReleaseTimeoutMs) {
                p->pttState.store(SM_PTT_READY);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

}  // namespace

/* ------------------------------------------------------------------ API */

extern "C" {

SM_API sm_phone* sm_phone_create(const char* host, uint16_t port, const char* session_id,
                                 const char* code, const char* identity_dir) {
    if (host == nullptr || session_id == nullptr || code == nullptr || port == 0) return nullptr;

    auto* p = new (std::nothrow) sm_phone();
    if (p == nullptr) return nullptr;

    p->identity = loadOrCreateIdentity(identity_dir ? identity_dir : "");
    p->host = host;
    p->port = port;
    p->sessionId = session_id;
    p->code = code;
    {
        std::lock_guard<std::mutex> lk(p->textMu);
        p->deviceId = p->identity.deviceId();
    }

    p->transport = std::make_shared<media::UdpTransport>("phone", 0);
    if (!p->transport->start() || !p->transport->setPeer(p->host, p->port)) {
        p->setError("cannot reach " + p->host + ":" + std::to_string(p->port));
        p->connState.store(SM_CONN_FAILED);
        return p;   /* handle is still valid so the UI can read the error */
    }

    p->peer = std::make_unique<session::PeerSession>(
        p->identity, security::PairingRole::Initiator, p->transport);

    session::SessionCallbacks cb;
    cb.onControl = [p](const protocol::Message& m) { onControl(p, m); };
    cb.onStateChanged = [p](session::SessionState s) {
        switch (s) {
        case session::SessionState::Pairing:
            p->connState.store(SM_CONN_PAIRING);
            break;
        case session::SessionState::Authenticated: {
            std::lock_guard<std::mutex> lk(p->textMu);
            p->peerFingerprint = p->peer->peerFingerprint();
            p->connState.store(SM_CONN_AUTHENTICATED);
            break;
        }
        case session::SessionState::Failed:
            p->connState.store(SM_CONN_FAILED);
            p->setError("pairing failed -- wrong code, or the code expired");
            break;
        default:
            break;
        }
    };
    p->peer->setCallbacks(cb);

    p->sender.open();

    p->capture = phone::createPlatformCapture();
    if (p->capture == nullptr ||
        !p->capture->start([p](const float* pcm, size_t n) { onCaptureFrame(p, pcm, n); })) {
        p->setError("microphone unavailable -- check the permission");
    }

    p->running.store(true);
    p->worker = std::thread(workerMain, p);
    p->peer->beginPairing(p->sessionId, p->code);
    return p;
}

SM_API void sm_phone_destroy(sm_phone* p) {
    if (p == nullptr) return;
    /* Releasing before teardown matters: a phone that exits mid-PTT should
       tell the PC rather than relying on the watchdog. */
    sm_phone_release(p);
    p->running.store(false);
    if (p->worker.joinable()) p->worker.join();
    if (p->capture) p->capture->stop();
    if (p->transport) p->transport->stop();
    delete p;
}

SM_API int sm_phone_conn_state(sm_phone* p) { return p ? p->connState.load() : SM_CONN_FAILED; }
SM_API int sm_phone_ptt_state(sm_phone* p) { return p ? p->pttState.load() : SM_PTT_FAILED; }
SM_API int sm_phone_rtt_ms(sm_phone* p) { return p ? p->rttMs.load() : -1; }
SM_API float sm_phone_mic_level(sm_phone* p) { return p ? p->micLevel.load() : 0.0f; }

SM_API int sm_phone_take_haptic(sm_phone* p) {
    if (p == nullptr) return SM_HAPTIC_NONE;
    return p->haptic.exchange(SM_HAPTIC_NONE);
}

SM_API void sm_phone_press(sm_phone* p) {
    if (p == nullptr || p->connState.load() != SM_CONN_AUTHENTICATED) return;
    if (p->pressed.exchange(true)) return;   /* idempotent */

    p->pressedAtMs.store(nowMs());
    p->pttState.store(SM_PTT_CONNECTING);
    p->sender.resetStream();
    p->peer->sendControl(protocol::Message::make(protocol::MsgType::StartPtt, p->sessionId,
                                                 p->identity.deviceId(), p->peer->nextSeq()));
}

SM_API void sm_phone_release(sm_phone* p) {
    if (p == nullptr) return;
    /* Deliberately NOT gated on `pressed`: every touch-loss path funnels here,
       and a release must never be a no-op that leaves the microphone open. */
    p->pressed.store(false);
    if (p->connState.load() != SM_CONN_AUTHENTICATED) {
        p->pttState.store(SM_PTT_READY);
        return;
    }
    p->releasedAtMs.store(nowMs());
    if (p->pttState.load() != SM_PTT_READY) p->pttState.store(SM_PTT_RELEASING);
    p->peer->sendControl(protocol::Message::make(protocol::MsgType::StopPtt, p->sessionId,
                                                 p->identity.deviceId(), p->peer->nextSeq(),
                                                 {{"reason", "release"}}));
}

SM_API void sm_phone_set_mode(sm_phone* p, int mode) {
    if (p == nullptr || p->connState.load() != SM_CONN_AUTHENTICATED) return;
    const char* name = "Auto";
    switch (mode) {
        case 1: name = "PhoneOnly"; break;
        case 2: name = "LocalOnly"; break;
        case 3: name = "Mute"; break;
        default: break;
    }
    p->peer->sendControl(protocol::Message::make(protocol::MsgType::SetMode, p->sessionId,
                                                 p->identity.deviceId(), p->peer->nextSeq(),
                                                 {{"mode", name}}));
}

SM_API const char* sm_phone_peer_fingerprint(sm_phone* p) {
    if (p == nullptr) return "";
    std::lock_guard<std::mutex> lk(p->textMu);
    p->outFingerprint = p->peerFingerprint;
    return p->outFingerprint.c_str();
}

SM_API const char* sm_phone_device_id(sm_phone* p) {
    if (p == nullptr) return "";
    std::lock_guard<std::mutex> lk(p->textMu);
    p->outDeviceId = p->deviceId;
    return p->outDeviceId.c_str();
}

SM_API const char* sm_phone_last_error(sm_phone* p) {
    if (p == nullptr) return "";
    std::lock_guard<std::mutex> lk(p->textMu);
    p->outError = p->lastError;
    return p->outError.c_str();
}

SM_API const char* sm_phone_pc_status(sm_phone* p) {
    if (p == nullptr) return "";
    std::lock_guard<std::mutex> lk(p->textMu);
    p->outStatus = p->pcStatus;
    return p->outStatus.c_str();
}

SM_API int sm_phone_parse_uri(const char* uri,
                              char* out_host, int host_len,
                              uint16_t* out_port,
                              char* out_session, int session_len,
                              char* out_code, int code_len,
                              char* out_fingerprint, int fp_len) {
    if (uri == nullptr) return 0;
    security::initCrypto();
    const auto parsed = security::parsePairingUri(uri);
    if (!parsed) return 0;

    auto copy = [](char* dst, int len, const std::string& src) {
        if (dst == nullptr || len <= 0) return;
        const int n = (int)std::min<size_t>(src.size(), (size_t)len - 1);
        std::memcpy(dst, src.data(), (size_t)n);
        dst[n] = '\0';
    };
    copy(out_host, host_len, parsed->host);
    copy(out_session, session_len, parsed->sessionId);
    copy(out_code, code_len, parsed->code);
    copy(out_fingerprint, fp_len, parsed->fingerprint);
    if (out_port) *out_port = parsed->port;
    return 1;
}

SM_API const char* sm_phone_version(void) { return kVersion; }

}  /* extern "C" */
