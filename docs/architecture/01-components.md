# 1. Final Component Architecture

## 1.1 Trust / process boundaries

```
┌─────────────────────── PHONE (Android / iOS) ────────────────────────┐
│  UI (Flutter)                                                        │
│    └─ MethodChannel / EventChannel                                   │
│  Native audio  (AudioRecord / AVAudioEngine)                         │
│  Native net    (WebRTC PeerConnection, DTLS-SRTP)                    │
│  Native crypto (Keystore / Keychain)   ── device identity keypair    │
│  Discovery     (NsdManager / NWBrowser)                              │
└──────────────────────────────┬───────────────────────────────────────┘
                               │ LAN
             control: TLS 1.3 / Noise-over-TCP (JSON, versioned)
             media:   WebRTC, Opus 48k mono, DTLS-SRTP
                               │
┌──────────────────────────────▼─────── WINDOWS PC ────────────────────┐
│                                                                      │
│  SmartMic.AudioService        (user mode, LocalService, C++20)       │
│   ├─ DiscoveryResponder       mDNS _smartmic._tcp                    │
│   ├─ PairingServer            SmartMic.Security                      │
│   ├─ ControlServer            SmartMic.Protocol                      │
│   ├─ PhoneMediaReceiver       WebRTC -> Opus decode -> PCM           │
│   ├─ JitterBuffer                                                    │
│   ├─ WasapiCaptureSource      physical mic (shared mode, event)      │
│   ├─ DeviceRegistry           enumerate + hotplug, excludes SmartMic │
│   ├─ AudioRouter  ★           state machine + crossfade + mixdown    │
│   ├─ Watchdog                 heartbeat, failback, self-heal         │
│   └─ DriverLink               PCM -> driver (or dev virtual cable)   │
│                                      │                               │
│                                      │ bounded shared ring + IOCTL   │
│                                      ▼                               │
│  SmartMic.Driver              (kernel, WDM/WaveRT, SysVAD-derived)   │
│   └─ Capture endpoint "Smart Microphone"  ── fails to SILENCE        │
│                                      │                               │
│                                      ▼                               │
│                    Teams · Zoom · Discord · Chrome · Edge            │
│                                                                      │
│  SmartMic.Desktop             (WinUI 3, C#) ── named pipe ──► service│
│  SmartMic.Installer           (WiX/MSI, dpinst/DIFx for driver)      │
└──────────────────────────────────────────────────────────────────────┘
```

★ = the only component that decides what audio the world hears. Everything
else feeds it or observes it.

## 1.2 Components and their single responsibility

| Component | Lang | Runs as | Responsibility | Explicitly NOT |
|---|---|---|---|---|
| `SmartMic.Driver` | C++ / WDK | kernel | Present one stable capture endpoint; drain a bounded PCM ring; emit silence on starvation | no network, no crypto, no Opus, no routing decisions, no allocation on the RT path |
| `SmartMic.AudioService` | C++20 | Windows service | Capture, receive, resample, route, crossfade, feed driver, own all state | no UI, no direct registry-of-truth for user prefs beyond its config file |
| `SmartMic.Desktop` | C# / WinUI 3 | user session | Visualise + edit service state, pairing UX, diagnostics | no audio path, no crypto material handling |
| `SmartMic.Protocol` | C++ + Dart + Kotlin/Swift codegen from one schema | lib | Wire types, versioning, replay window, idempotency | no transport |
| `SmartMic.Security` | C++ / platform crypto | lib | Device identity, pairing, key storage, revocation | no protocol framing |
| `SmartMic.Installer` | WiX | — | install/repair/uninstall service+driver+app, capture pre-install default mic | no first-run policy decisions |

## 1.3 The recursion guard (non-negotiable invariant)

`DeviceRegistry` filters out any endpoint whose container/device ID matches the
SmartMic virtual endpoint before it is ever offered as a local source, and
`AudioRouter::setLocalSource()` rejects it a second time. The installer records
the pre-install default capture endpoint as `preferredLocalInputId` so the
service never has to ask Windows "what is the default mic?" after it has itself
become the default. This is enforced in code (`DeviceRegistry`) and in a unit
test (`SelfSelectionIsRejected`).
