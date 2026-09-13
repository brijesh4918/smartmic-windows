// smartmic-router -- Phase 1 harness.
//
//   smartmic-router demo <out.wav>      offline: scripted PTT cycle, writes an
//                                       audible artifact. Runs anywhere.
//   smartmic-router list                Windows only: enumerate capture devices
//   smartmic-router run --in <id> --out <id>
//                                       Windows only: live physical mic ->
//                                       development virtual cable, with the
//                                       router in between and PTT driven from
//                                       stdin (p = press, r = release, m = mode)
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "host_sources.h"
#include "smartmic/logging.h"
#include "smartmic/router.h"

#if defined(SMARTMIC_HAVE_WASAPI)
#include "wasapi_capture_source.h"
#include "wasapi_device_enumerator.h"
#include "wasapi_render_sink.h"
#include <atomic>
#include <iostream>
#include <thread>
#endif

using namespace smartmic;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  smartmic-router demo <out.wav>\n"
#if defined(SMARTMIC_HAVE_WASAPI)
                 "  smartmic-router list\n"
                 "  smartmic-router run --in <deviceId> --out <deviceId>\n"
#endif
    );
    return 2;
}

// Offline scenario. Every interesting transition, rendered to one file so a
// human can listen for clicks -- which is the acceptance criterion that
// automated tests can only approximate.
int runDemo(const std::string& outPath) {
    DeviceRegistry registry;
    registry.setDevices({
        DeviceInfo{"{local}", "Simulated local microphone (220 Hz)", true, false, 48000, 1},
        DeviceInfo{"{smartmic}", "Smart Microphone", false, true, 48000, 1},
    });
    registry.setPreferredInput("{local}");

    auto local = std::make_shared<host::ToneSource>("local", 220.0f, 0.45f);
    auto phone = std::make_shared<host::ToneSource>("phone", 660.0f, 0.45f);
    auto sink = std::make_shared<host::WavFileSink>("wav", outPath);
    if (!sink->start()) return 1;
    local->start();
    phone->start();

    RouterConfig cfg;
    cfg.crossfadeMs = 15;
    AudioRouter router(cfg, &registry);
    router.setLocalSource(local);
    router.setPhoneSource(phone);
    router.setSink(sink);

    auto seconds = [](double s) { return static_cast<int>(s * 1000.0 / kFrameMillis); };
    auto hold = [&](int frames, bool heartbeat) {
        for (int i = 0; i < frames; ++i) {
            if (heartbeat) router.notifyPhoneHeartbeat();
            router.tick();
        }
    };

    std::printf("scripted scenario -> %s\n", outPath.c_str());
    std::printf("normal source: %s\n\n", router.effectiveInputName().c_str());

    std::printf("  0.0s  local microphone (220 Hz)\n");
    hold(seconds(1.0), false);

    std::printf("  1.0s  PTT pressed, phone acknowledges (660 Hz)\n");
    router.startPtt();
    router.tick();
    router.notifyPhoneReady();
    hold(seconds(1.5), true);

    std::printf("  2.5s  PTT released\n");
    router.stopPtt();
    hold(seconds(1.0), false);

    std::printf("  3.5s  PTT pressed again, then the phone dies mid-stream\n");
    router.startPtt();
    router.tick();
    router.notifyPhoneReady();
    hold(seconds(1.0), true);
    hold(seconds(1.5), false);   // no heartbeat: watchdog must fail back

    std::printf("  6.0s  mode -> Mute, then Local Only, then Auto\n");
    router.setMode(RoutingMode::Mute);
    hold(seconds(0.6), false);
    router.setMode(RoutingMode::LocalOnly);
    hold(seconds(0.6), false);
    router.setMode(RoutingMode::Auto);
    hold(seconds(0.8), false);

    sink->stop();

    const RouterStats s = router.stats();
    std::printf(
        "\nframes=%llu  transitions=%llu  phoneLost=%llu  armTimeouts=%llu"
        "  sourceFailures=%llu  sinkFailures=%llu  limited=%llu  finalState=%s\n",
        static_cast<unsigned long long>(s.framesProduced),
        static_cast<unsigned long long>(s.transitions),
        static_cast<unsigned long long>(s.phoneLostEvents),
        static_cast<unsigned long long>(s.armTimeouts),
        static_cast<unsigned long long>(s.sourceFailures),
        static_cast<unsigned long long>(s.sinkFailures),
        static_cast<unsigned long long>(s.limitedSamples),
        std::string(toString(s.state)).c_str());

    if (s.phoneLostEvents != 1) {
        std::fprintf(stderr, "expected exactly one watchdog failback\n");
        return 1;
    }
    return 0;
}

#if defined(SMARTMIC_HAVE_WASAPI)

int runList() {
    WasapiDeviceEnumerator en;
    if (!en.initialise()) return 1;
    std::printf("capture devices:\n");
    for (const auto& d : en.enumerateCapture()) {
        std::printf("  %-42s %s%s%s\n", d.id.c_str(), d.name.c_str(),
                    d.isDefault ? "  [default]" : "",
                    d.isSmartMicEndpoint ? "  [SmartMic -- never selectable as input]" : "");
    }
    std::printf("\nrender devices (virtual cable candidates):\n");
    for (const auto& d : en.enumerateRender()) {
        std::printf("  %-42s %s\n", d.id.c_str(), d.name.c_str());
    }
    return 0;
}

int runLive(const std::string& inId, const std::string& outId) {
    DeviceRegistry registry;
    WasapiDeviceEnumerator en;
    if (!en.initialise()) return 1;
    registry.setDevices(en.enumerateCapture());
    if (!registry.setPreferredInput(inId)) {
        std::fprintf(stderr, "refused or unknown capture device: %s\n", inId.c_str());
        return 1;
    }

    auto local = std::make_shared<WasapiCaptureSource>("local", inId);
    auto phone = std::make_shared<host::ToneSource>("phone", 660.0f, 0.4f);  // stands in for Phase 2
    auto sink = std::make_shared<WasapiRenderSink>("cable", outId);
    if (!local->start() || !sink->start()) return 1;
    phone->start();

    AudioRouter router(RouterConfig{}, &registry);
    router.setLocalSource(local);
    router.setPhoneSource(phone);
    router.setSink(sink);

    // Hot-plug: keep the inventory current so the desktop app and the
    // auto-prefer rule see reality.
    en.onDevicesChanged([&] { registry.setDevices(en.enumerateCapture()); });

    std::atomic<bool> running{true};
    std::thread audio([&] {
        while (running.load()) {
            router.tick();
            if (router.state() == SourceState::PhoneActive) router.notifyPhoneHeartbeat();
        }
    });

    std::printf("p=press  r=release  1=Auto 2=PhoneOnly 3=LocalOnly 4=Mute  q=quit\n");
    for (std::string line; std::getline(std::cin, line);) {
        if (line == "p") router.startPtt();
        else if (line == "r") router.stopPtt();
        else if (line == "1") router.setMode(RoutingMode::Auto);
        else if (line == "2") router.setMode(RoutingMode::PhoneOnly);
        else if (line == "3") router.setMode(RoutingMode::LocalOnly);
        else if (line == "4") router.setMode(RoutingMode::Mute);
        else if (line == "q") break;
        const auto s = router.stats();
        std::printf("state=%s mode=%s frames=%llu\n", std::string(toString(s.state)).c_str(),
                    std::string(toString(s.mode)).c_str(),
                    static_cast<unsigned long long>(s.framesProduced));
    }
    running.store(false);
    audio.join();
    local->stop();
    sink->stop();
    return 0;
}

#endif  // SMARTMIC_HAVE_WASAPI

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];

    if (cmd == "demo") {
        if (argc < 3) return usage();
        return runDemo(argv[2]);
    }
#if defined(SMARTMIC_HAVE_WASAPI)
    if (cmd == "list") return runList();
    if (cmd == "run") {
        std::string in, out;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (std::strcmp(argv[i], "--in") == 0) in = argv[i + 1];
            else if (std::strcmp(argv[i], "--out") == 0) out = argv[i + 1];
        }
        if (in.empty() || out.empty()) return usage();
        return runLive(in, out);
    }
#endif
    return usage();
}
