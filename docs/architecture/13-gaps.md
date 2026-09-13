# 13. What Is Not Built Yet

Written so that nobody has to reverse-engineer the gaps from the code.

## Not started

| Area | Why it is not done | What unblocks it |
|---|---|---|
| **SmartMic.Driver** (kernel virtual mic) | Needs MSVC + WDK + a Windows test machine. The development virtual cable stands in behind `IDriverLink`, exactly as ADR-001 planned. | A Windows 11 box with Visual Studio + WDK, and a second machine or VM for test-signed deployment |
| **WASAPI capture / render** | Written (`windows/audio-service/platform/windows/`) but **never compiled or run** — this repository has been developed on macOS. Treat it as a first draft. | The same Windows box |
| **Android app** | Needs the Android toolchain and a WebRTC or transport binding. The C++ `smartmic-phone` simulator speaks the real protocol, so the app is a UI + `AudioRecord` + JNI binding, not a redesign. | Android Studio + a device; decision on Flutter vs native-only (ADR-009 assumes Flutter) |
| **iOS app** | Same, with `AVAudioEngine` and Network Framework. | Xcode + a device + an Apple Developer account |
| **WinUI 3 desktop app** | No audio-path risk; it is a view over the service's state. | The Windows box |
| **mDNS / DNS-SD discovery** | Deliberately deferred rather than half-built: a hand-rolled partial mDNS responder would be worse than none. The QR/manual path covers pairing today. | Decide between a platform API (Bonjour on Windows) and a vendored responder |
| **Installer (WiX/MSI)** | Depends on the driver existing. | Phase 5 |
| **Multi-PC management** | The session layer is per-peer already; what is missing is the registry of known PCs and the "active PC" concept, which lives in the mobile app. | Phase 4 |
| **Reconnect without re-pairing** | `PeerSession::adoptSessionKey()` exists and long-term keys are persisted, but the challenge–response reconnect flow is not wired up; the demo binaries pair every run. | Small, self-contained |

## Deviations from the original brief, and why

| Brief said | Built instead | Recorded in |
|---|---|---|
| WebRTC for media | `ITransport` + libsodium-sealed UDP as the Phase 2/3 proving vehicle; WebRTC remains the production implementation behind the same interface | ADR-011 |
| PAKE (unspecified) | CPace over ristretto255, because libsodium ships no PAKE but does ship the group operations one needs | ADR-012 |
| Flutter shell | Nothing mobile built yet; the decision stands unchanged | ADR-009 |

## Things that are written but unverified

Everything under `windows/audio-service/platform/windows/`. It compiles under no
compiler available here. The first Windows build should be treated as a
bring-up, not a regression run.

## Release-blocking items already known

1. External cryptographic review of `shared/security/src/pairing.cpp` (ADR-012).
2. EV certificate + Partner Center account (ADR-010, risk R1) — procurement, start now.
3. Driver Verifier + HLK runs (ADR-010).
4. The manual Windows acceptance matrix M1–M7 (`docs/testing/11-acceptance-tests.md`).
