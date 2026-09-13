// smartmic-service -- the Windows audio service, minus the Windows.
//
// On Windows it captures the selected physical microphone through WASAPI and
// feeds a development virtual cable. Everywhere else it uses a tone as the
// local microphone and writes the router's output to a WAV file, so the whole
// system above the platform audio layer can be run and heard on any machine.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "host_sources.h"
#if defined(SMARTMIC_HAVE_LIVE_MIC)
#include "live_mic_source.h"
#endif
#include "smartmic/media/phone_media_source.h"
#include "smartmic/media/udp_transport.h"
#include "smartmic/router.h"
#include "smartmic/session/peer_session.h"

#if defined(SMARTMIC_HAVE_WASAPI)
#include "wasapi_capture_source.h"
#include "wasapi_device_enumerator.h"
#include "wasapi_render_sink.h"
#include "smartmic_driver_link.h"
#include "driver_diagnostics.h"
#endif

using namespace smartmic;
namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }

// The identity is the credential every future connection is authenticated
// against, so it is created once and kept. On Windows this file is replaced by
// DPAPI-protected storage (ADR-005); the file path here is the dev stand-in and
// is created with owner-only permissions.
security::DeviceIdentity loadOrCreateIdentity(const fs::path& stateDir) {
    security::initCrypto();
    const fs::path keyFile = stateDir / "identity.key";
    std::error_code ec;
    fs::create_directories(stateDir, ec);

    if (fs::exists(keyFile)) {
        std::ifstream in(keyFile, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        if (auto id = security::DeviceIdentity::fromSecretKey(bytes)) return *id;
        std::fprintf(stderr, "identity file is unreadable; generating a new one\n");
    }

    const auto id = security::DeviceIdentity::generate();
    std::ofstream out(keyFile, std::ios::binary);
    out.write(reinterpret_cast<const char*>(id.secretKey().data()),
              static_cast<std::streamsize>(id.secretKey().size()));
    out.close();
    fs::permissions(keyFile, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
    return id;
}

#if defined(_WIN32)
using sm_socklen_t = int;
#define SM_CLOSE_SOCK closesocket
#else
using sm_socklen_t = socklen_t;
#define SM_CLOSE_SOCK ::close
#endif

// The address a phone on the same Wi-Fi must actually dial.
//
// This used to return the hostname. On a Mac that looks fine, because Bonjour
// resolves "something.local". On Windows it yields a NetBIOS name like
// DESKTOP-ABC123 that an iPhone usually cannot resolve at all, so the pairing
// link pointed at a host the phone could never reach.
//
// Connecting a UDP socket sends no packets; it only makes the OS choose a
// route, and therefore a source address. That source address is the right
// answer, and it works the same way on every platform.
std::string primaryLanAddress() {
    const int sock = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, 0));
    if (sock < 0) return {};

    sockaddr_in probe{};
    probe.sin_family = AF_INET;
    probe.sin_port = htons(53);
    probe.sin_addr.s_addr = inet_addr("203.0.113.1");   // TEST-NET-3: never routed

    std::string result;
    if (::connect(sock, reinterpret_cast<sockaddr*>(&probe), sizeof(probe)) == 0) {
        sockaddr_in local{};
        sm_socklen_t len = sizeof(local);
        if (::getsockname(sock, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
            char text[INET_ADDRSTRLEN] = {0};
            if (::inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)) != nullptr) {
                result = text;
            }
        }
    }
    SM_CLOSE_SOCK(sock);
    if (result == "0.0.0.0") result.clear();
    return result;
}

// Everything else this machine is reachable by, so the user has something to
// try when the primary guess is the wrong network (VPNs and Hyper-V switches
// both love to win the routing table).
std::vector<std::string> otherLocalAddresses(const std::string& primary) {
    std::vector<std::string> out;
    char host[256] = {0};
    if (::gethostname(host, sizeof(host) - 1) != 0) return out;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, nullptr, &hints, &res) != 0 || res == nullptr) return out;

    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        char text[INET_ADDRSTRLEN] = {0};
        auto* v4 = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        if (::inet_ntop(AF_INET, &v4->sin_addr, text, sizeof(text)) == nullptr) continue;
        const std::string addr = text;
        if (addr == primary || addr.rfind("127.", 0) == 0) continue;
        if (std::find(out.begin(), out.end(), addr) == out.end()) out.push_back(addr);
    }
    ::freeaddrinfo(res);
    return out;
}

std::string localAddressHint() {
    const std::string ip = primaryLanAddress();
    if (!ip.empty()) return ip;
    // No usable route: fall back to the hostname rather than printing nothing.
    char host[256] = {0};
    if (::gethostname(host, sizeof(host) - 1) == 0 && host[0]) return host;
    return "127.0.0.1";
}

// Where the router's output goes.
//   auto   - the SmartMic driver if it is there, otherwise a WAV file
//   driver - the SmartMic driver, and fail loudly if it is missing
//   wav    - a WAV file (works with no driver at all; good for a first test)
//   cable  - a third-party virtual cable's render endpoint, by device id
enum class SinkKind { Auto, Driver, Wav, Cable };

struct Options {
    uint16_t port = 47820;
    std::string stateDir;
    std::string outWav = "smartmic-output.wav";
    SinkKind sink = SinkKind::Auto;
    std::string cableId;
    int durationSeconds = 0;      // 0 = until interrupted
    std::string forcedCode;       // test hook only
    std::string forcedSession;
    bool diagnoseOnly = false;
};

void printUsage() {
    std::printf(
        "smartmic-service -- the SmartMic PC service\n\n"
        "  --port <n>          UDP port to listen on (default 47820)\n"
        "  --sink <kind>       auto | driver | wav | cable   (default auto)\n"
        "  --out <path|id>     WAV path for --sink wav, device id for --sink cable\n"
        "  --duration <secs>   stop after this long (default: run until Ctrl+C)\n"
        "  --state <dir>       where to keep this PC's identity key\n"
        "  --diagnose          report the driver's state and exit\n"
        "  --help\n");
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    opt.stateDir = (fs::temp_directory_path() / "smartmic-service").string();
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto value = [&]() -> std::string {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
        };
        if (k == "--help" || k == "-h") { printUsage(); return 0; }
        else if (k == "--diagnose") opt.diagnoseOnly = true;
        else if (k == "--port") opt.port = static_cast<uint16_t>(std::stoi(value()));
        else if (k == "--state") opt.stateDir = value();
        else if (k == "--out") opt.outWav = value();
        else if (k == "--duration") opt.durationSeconds = std::stoi(value());
        else if (k == "--code") opt.forcedCode = value();
        else if (k == "--session") opt.forcedSession = value();
        else if (k == "--sink") {
            const std::string v = value();
            if (v == "auto") opt.sink = SinkKind::Auto;
            else if (v == "driver") opt.sink = SinkKind::Driver;
            else if (v == "wav") opt.sink = SinkKind::Wav;
            else if (v == "cable") opt.sink = SinkKind::Cable;
            else { std::fprintf(stderr, "unknown --sink '%s'\n", v.c_str()); return 2; }
        }
        else { std::fprintf(stderr, "unknown option '%s'\n\n", k.c_str()); printUsage(); return 2; }
    }

#if defined(SMARTMIC_HAVE_WASAPI)
    if (opt.diagnoseOnly) {
        const auto d = diagnoseDriver();
        std::printf("SmartMic driver diagnosis\n\n%s", formatDriverDiagnosis(d).c_str());
        return d.interfacePresent ? 0 : 1;
    }
#else
    if (opt.diagnoseOnly) {
        std::printf("--diagnose only means something on Windows; there is no "
                    "SmartMic driver on this platform.\n");
        return 0;
    }
#endif
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const auto identity = loadOrCreateIdentity(opt.stateDir);

    // --- audio -------------------------------------------------------------
    DeviceRegistry registry;
    std::shared_ptr<IAudioSource> local;
    std::shared_ptr<IAudioSink> sink;
    // The live-microphone path has to open the device to know whether it works
    // at all, so it is already started by the time we get to the common start
    // below. Opening it twice would take a second handle on the microphone.
    bool localAlreadyStarted = false;
    // The driver path has to open the device to know whether it works, so it
    // may already be started by the time we reach the common start below.
    bool sinkStarted = false;

#if defined(SMARTMIC_HAVE_WASAPI)
    WasapiDeviceEnumerator enumerator;
    enumerator.initialise();
    registry.setDevices(enumerator.enumerateCapture());
    const auto chosen = registry.resolveEffectiveInput();
    if (!chosen) { std::fprintf(stderr, "no usable capture device\n"); return 1; }
    local = std::make_shared<WasapiCaptureSource>("local", chosen->id);
    
    // Pick the output explicitly rather than inferring it from whether --out
    // happens to still be the default -- that was impossible to reason about.
    if (opt.sink == SinkKind::Cable) {
        sink = std::make_shared<WasapiRenderSink>("cable", opt.outWav);
    } else if (opt.sink == SinkKind::Wav) {
        sink = std::make_shared<host::WavFileSink>("wav", opt.outWav);
    } else {
        auto link = std::make_shared<SmartMicDriverLink>("driver");
        if (link->start()) {
            sink = link;
            sinkStarted = true;
            std::printf("  output     Smart Microphone (driver v%u)\n", link->driverVersion());
        } else if (opt.sink == SinkKind::Driver) {
            // The user asked for the driver specifically, so do not silently
            // do something else -- explain and stop.
            const auto d = diagnoseDriver();
            std::fprintf(stderr, "\nCannot use the SmartMic driver.\n\n%s\n",
                         formatDriverDiagnosis(d).c_str());
            return 1;
        } else {
            // Auto: keep running. Everything except the hand-off to Windows
            // still works, which is enough to pair a phone and hear the result.
            const auto d = diagnoseDriver();
            std::printf("\n--------------------------------------------------------------\n");
            std::printf("%s", formatDriverDiagnosis(d).c_str());
            std::printf("\n  Falling back to recording into a file, so you can still pair\n"
                        "  the phone and check the audio. Teams and Zoom will NOT see a\n"
                        "  SmartMic microphone until the driver loads.\n");
            std::printf("  Run with --sink driver to make this an error instead.\n");
            std::printf("--------------------------------------------------------------\n\n");
            const std::string wav =
                (opt.outWav == "smartmic-output.wav") ? std::string("smartmic-output.wav")
                                                      : opt.outWav;
            sink = std::make_shared<host::WavFileSink>("wav", wav);
        }
    }
#else
    registry.setDevices({
        DeviceInfo{"{local}", "Simulated local microphone (317 Hz)", true, false, 48000, 1},
        DeviceInfo{"{smartmic}", "Smart Microphone", false, true, 48000, 1},
    });
    registry.setPreferredInput("{local}");
#if defined(SMARTMIC_HAVE_LIVE_MIC)
    // Prefer this machine's real microphone so the recording shows two actual
    // voices switching, not a tone. Falls back to the tone if the microphone
    // is unavailable -- the router must still have a local source.
    {
        auto mic = std::make_shared<host::LiveMicSource>("local");
        if (mic->start()) {
            local = mic;
            localAlreadyStarted = true;
            std::printf("  local mic  %s\n", mic->backend());
        } else {
            std::printf("  local mic  unavailable, using a 317 Hz tone instead\n");
            local = std::make_shared<host::ToneSource>("local", 317.0f, 0.45f);
        }
    }
#else
    local = std::make_shared<host::ToneSource>("local", 317.0f, 0.45f);
#endif
    sink = std::make_shared<host::WavFileSink>("wav", opt.outWav);
#endif
    if (!sinkStarted && !sink->start()) {
        std::fprintf(stderr, "failed to open the output (%s)\n", sink->id().c_str());
        return 1;
    }
    if (!localAlreadyStarted && !local->start()) {
        std::fprintf(stderr, "failed to open the local microphone\n");
        return 1;
    }

    auto phoneSource = std::make_shared<media::PhoneMediaSource>("phone");
    phoneSource->start();

    AudioRouter router(RouterConfig{}, &registry);
    router.setLocalSource(local);
    router.setPhoneSource(phoneSource);
    router.setSink(sink);

    // --- network + pairing --------------------------------------------------
    auto transport = std::make_shared<media::UdpTransport>("service", opt.port);
    if (!transport->start()) { std::fprintf(stderr, "cannot bind UDP %u\n", opt.port); return 1; }

    security::PairingOfferBook offers;
    // --code / --session make the demo reproducible without a human reading a
    // screen. Omitted, a fresh random code and session are generated.
    const auto offer = offers.issue(
        identity, localAddressHint(), transport->localPort(), protocol::nowUnixMs(),
        opt.forcedCode.empty() ? std::nullopt : std::optional<std::string>(opt.forcedCode),
        opt.forcedSession.empty() ? std::nullopt : std::optional<std::string>(opt.forcedSession));

    session::PeerSession peer(identity, security::PairingRole::Responder, transport);
    peer.armPairing(offer.sessionId, offer.code, &offers);

    std::atomic<bool> pttRequested{false};
    std::atomic<bool> announcedReady{false};
    std::atomic<uint64_t> lastMediaMs{0};

    session::SessionCallbacks cb;
    cb.onMedia = [&](const uint8_t* data, size_t len) {
        const uint64_t now = protocol::nowUnixMs();
        lastMediaMs.store(now);
        phoneSource->onMediaPayload(data, len, now);
        // Media arrival is liveness. A heartbeat the phone cannot actually
        // deliver would not be liveness (ADR-008).
        router.notifyPhoneHeartbeat();
    };
    cb.onControl = [&](const protocol::Message& m) {
        using protocol::MsgType;
        switch (m.type) {
            case MsgType::StartPtt:
                pttRequested.store(true);
                announcedReady.store(false);
                router.startPtt();
                peer.sendControl(protocol::Message::make(MsgType::PttPreparing, m.sessionId,
                                                         identity.deviceId(), peer.nextSeq()));
                break;
            case MsgType::StopPtt:
                pttRequested.store(false);
                router.stopPtt();
                peer.sendControl(protocol::Message::make(MsgType::PttStopped, m.sessionId,
                                                         identity.deviceId(), peer.nextSeq()));
                break;
            case MsgType::SetMode: {
                const std::string mode = m.payload.value("mode", "Auto");
                if (mode == "PhoneOnly") router.setMode(RoutingMode::PhoneOnly);
                else if (mode == "LocalOnly") router.setMode(RoutingMode::LocalOnly);
                else if (mode == "Mute") router.setMode(RoutingMode::Mute);
                else router.setMode(RoutingMode::Auto);
                break;
            }
            case MsgType::Ping:
                peer.sendControl(protocol::Message::make(MsgType::Pong, m.sessionId,
                                                         identity.deviceId(), peer.nextSeq()));
                break;
            case MsgType::GetStatus: {
                const auto st = router.stats();
                peer.sendControl(protocol::Message::make(
                    MsgType::Status, m.sessionId, identity.deviceId(), peer.nextSeq(),
                    {{"routerState", std::string(toString(st.state))},
                     {"mode", std::string(toString(st.mode))},
                     {"localSourceName", router.effectiveInputName()},
                     {"driverReady", true}}));
                break;
            }
            default:
                break;
        }
    };
    cb.onStateChanged = [&](session::SessionState s) {
        if (s == session::SessionState::Authenticated) {
            std::printf("\n  paired with phone %s\n  fingerprint %s\n\n",
                        peer.peerDeviceId().c_str(), peer.peerFingerprint().c_str());
        }
    };
    peer.setCallbacks(cb);

    std::printf("SmartMic service\n");
    std::printf("  device     %s\n", identity.deviceId().c_str());
    std::printf("  fingerprint %s\n", identity.fingerprint().c_str());
    std::printf("  listening  udp/%u\n", transport->localPort());
    std::printf("  output     %s\n", opt.outWav.c_str());
    std::printf("\n  pairing code  %s   (expires in 5 minutes)\n", offer.code.c_str());
    std::printf("  session       %s\n", offer.sessionId.c_str());
    std::printf("  qr            %s\n", offer.qrUri.c_str());
    {
        const auto others = otherLocalAddresses(localAddressHint());
        if (!others.empty()) {
            std::printf("\n  If the phone cannot reach %s, this PC is also at:",
                        localAddressHint().c_str());
            for (const auto& a : others) std::printf(" %s", a.c_str());
            std::printf("\n");
        }
    }
    std::printf("\n");
    std::fflush(stdout);

    // --- audio loop ---------------------------------------------------------
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    const auto period = std::chrono::milliseconds(kFrameMillis);
    const auto deadline = opt.durationSeconds > 0
                              ? clock::now() + std::chrono::seconds(opt.durationSeconds)
                              : clock::time_point::max();

    while (!g_stop.load() && clock::now() < deadline) {
        // The service's PTT_READY is emitted only once the jitter buffer holds
        // real decodable audio -- never on the request alone (ADR-007).
        if (pttRequested.load() && !announcedReady.load() &&
            router.state() == SourceState::PhoneArming && phoneSource->ready()) {
            router.notifyPhoneReady();
            peer.sendControl(protocol::makePttReady(offer.sessionId, identity.deviceId(),
                                                    peer.nextSeq(), protocol::nowUnixMs()));
            announcedReady.store(true);
        }
        router.tick();
        next += period;
        std::this_thread::sleep_until(next);
    }

    const auto st = router.stats();
    const auto js = phoneSource->jitterStats();
    std::printf("\nstopping.\n");
    std::printf("  frames=%llu transitions=%llu phoneLost=%llu armTimeouts=%llu state=%s\n",
                (unsigned long long)st.framesProduced, (unsigned long long)st.transitions,
                (unsigned long long)st.phoneLostEvents, (unsigned long long)st.armTimeouts,
                std::string(toString(st.state)).c_str());
    std::printf("  jitter: pushed=%llu popped=%llu concealed=%llu dup=%llu late=%llu target=%ums\n",
                (unsigned long long)js.pushed, (unsigned long long)js.popped,
                (unsigned long long)js.concealed, (unsigned long long)js.duplicates,
                (unsigned long long)js.tooLate, js.targetMs);
    std::printf("  session: rejectedDatagrams=%llu pairingFailures=%llu\n",
                (unsigned long long)peer.rejectedDatagrams(),
                (unsigned long long)peer.pairingFailures());

    sink->stop();
    local->stop();
    phoneSource->stop();
    transport->stop();
    return 0;
}
