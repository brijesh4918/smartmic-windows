# 9. Technology Choices

| Area | Choice | Why | Rejected |
|---|---|---|---|
| Virtual mic | SysVAD-derived WDM/WaveRT capture miniport | The documented Microsoft path for a virtual audio device; gives a stable endpoint that survives source changes | APO/effects (wrong layer, can't source audio); user-mode driver frameworks (no supported user-mode audio capture endpoint); repeated default-endpoint switching (undocumented, visibly breaks running apps) |
| PC capture | WASAPI shared mode, event-driven | Documented, low latency, shared mode coexists with Teams holding the same device | DirectSound/MME (legacy, higher latency); exclusive mode (would lock the mic away from other apps) |
| Service language | C++20 | Real-time audio path with no GC pauses; direct WASAPI/WDK interop | C# (GC on the audio thread); Rust (better fit technically, but the driver and WASAPI story force C++ anyway and one language is cheaper) |
| Transport | WebRTC | Gets DTLS-SRTP, ICE, congestion control, NACK/PLC and a migration path to remote mode in one dependency | Raw UDP + custom crypto (rebuilding the hard parts badly); WebSocket audio (TCP head-of-line blocking is fatal for live voice) |
| Codec | Opus 48 kHz mono, 20 ms | Designed for this; excellent at 24–32 kbps; built-in PLC/FEC; native rate matches canonical | AAC-LD (licensing, worse PLC); raw PCM (≈768 kbps, bad on Wi-Fi) |
| Canonical format | 48 kHz mono float32, 960-sample frames | Matches Opus and the Windows mix format; float gives crossfade headroom | 16 kHz (audibly worse); 44.1 kHz (forces a resample at both ends) |
| Discovery | mDNS / DNS-SD `_smartmic._tcp` | Zero-config, first-class on both mobile OSes (NsdManager / NWBrowser) | Broadcast UDP (blocked more often, no service model); cloud-only (defeats LAN MVP) |
| Mobile shell | Flutter | One UI codebase; the UI is not the hard part | Two native UIs (2× cost for the easy half) |
| Mobile audio/net | Native Kotlin / Swift behind a narrow channel | Audio capture, interruptions, foreground-service rules and WebRTC lifecycle are where Flutter plugins are least trustworthy | Pure-Dart audio (no credible low-latency capture) |
| Desktop UI | WinUI 3 / C# | Native look, fast to build, strictly out of the audio path | Electron (weight); WPF (older stack) |
| Installer | WiX + DIFx/PnP driver install | Signed MSI, driver + service + app, real repair/uninstall | NSIS/Inno (weaker driver + service story) |
| Key storage | DPAPI (Win) / Keystore (Android) / Keychain (iOS) | OS-protected, hardware-backed where available | Files on disk with a hardcoded key |
| Pairing | PAKE (SPAKE2) over a 6-digit code | The only way a 20-bit secret is safe; no offline brute force | Sending the code and comparing (replayable, MITM-able) |

## Build

CMake ≥ 3.24 for the C++ tree. The portable core compiles on clang/gcc/MSVC so
the state machine, ring buffer, crossfade and resampler are unit-tested on any
developer machine and in CI; only `platform/windows/` requires MSVC.
