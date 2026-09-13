# 10. Phase 1 Implementation Plan

## Goal

Prove the single riskiest product assumption before any kernel code exists:

> A user-mode router can switch the audio source feeding one stable Windows
> capture endpoint, mid-call, inaudibly, without any application noticing.

Phase 1 has **no phone, no network, no crypto, no driver**. Two *local* sources
and a development virtual cable.

## Deliverables

1. **Portable core** (`windows/audio-service/{include,src}`) — builds on
   clang/gcc/MSVC, no OS audio dependency:
   - `AudioFrame` / canonical format (48 k mono f32, 960-sample frames)
   - `RingBuffer<T>` — bounded SPSC, monotonic 64-bit indices, mask-wrapped,
     acquire/release ordering, overwrite-oldest on overrun, count both
   - `LinearResampler` — arbitrary rate → 48 k, stateful across blocks
   - `Crossfade` — equal-power, configurable 5–100 ms, sample-accurate
   - `SourceStateMachine` — the 8 states from the brief, with the guards
   - `AudioRouter` — owns sources, gains, fade, watchdog tick, sink write
   - `DeviceRegistry` — inventory + hotplug + **self-selection rejection**
   - `IAudioSource` / `IAudioSink` / `IDriverLink` interfaces
2. **Host backends** (`platform/host/`) — `ToneSource`, `WavFileSource`,
   `WavFileSink`, `SilenceSource`. These make the core testable and audible on
   any machine, including this one, and are the CI substrate.
3. **Windows backends** (`platform/windows/`) — `WasapiCaptureSource`,
   `WasapiRenderSink` (→ dev virtual cable), `WasapiDeviceEnumerator` with
   `IMMNotificationClient` hotplug. Compiled only under MSVC.
4. **CLI harness** (`app/`) — `smartmic-router`: list devices, run the router,
   drive mode/source changes from stdin, print live stats. On Windows it wires
   WASAPI→virtual cable; on other hosts it wires tone/WAV→WAV so the crossfade
   can be listened to.
5. **Unit tests** — dependency-free harness, run in CI on every platform.

## Sequence

| Step | Work | Gate |
|---|---|---|
| 1.1 | Format, frame, ring buffer | ring tests pass incl. threaded soak |
| 1.2 | Resampler + crossfade | no discontinuity > threshold at the seam |
| 1.3 | State machine | every illegal transition rejected |
| 1.4 | Router + registry | offline render produces a clean switch |
| 1.5 | Host backends + CLI | audible WAV artifact of a live switch |
| 1.6 | WASAPI backends | (Windows box) real mic → virtual cable |
| 1.7 | App verification | Teams/Zoom/Chrome/Discord acceptance below |

## Explicitly out of scope for Phase 1

Networking, pairing, WebRTC, Opus, mDNS, the WinUI app, the installer, the
kernel driver, multi-PC, and anything on the phone. Interfaces are shaped so
these land without touching `AudioRouter`.
