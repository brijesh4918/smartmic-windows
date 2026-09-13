# ADR-009 — Mobile Cross-Platform / Native Boundary

**Status:** Accepted · **Date:** 2026-09-12

## Context
One UI on two platforms is worth having. One *audio and lifecycle
implementation* on two platforms is not achievable at the reliability this
product needs — the failure modes live precisely in the platform-specific parts.

## Decision
Flutter owns UI, navigation, and view state. Native owns everything that can
fail in a platform-specific way, behind two narrow interfaces:

```
AudioCaptureEngine : start() stop() setGain() setProcessingMode() audioFrames
ConnectionEngine   : discoverPCs() pair() connect() disconnect()
                     startPTT() stopPTT() statusStream
```

Android: Kotlin — `AudioRecord`, `microphone` foreground service type,
`NsdManager`, Android Keystore, WebRTC. iOS: Swift — `AVAudioEngine` input tap,
`AVAudioSession` (`playAndRecord`, voice-processing evaluated in Phase 4),
`NWBrowser`, Keychain, WebRTC.

## Alternatives
1. **Fully native ×2.** Doubles the cost of the cheap half. Rejected.
2. **Pure Dart audio + community plugins.** No credible low-latency capture,
   and interruption/foreground-service semantics are exactly what plugins get
   wrong. Rejected.
3. **React Native / KMP.** No advantage over Flutter here for the UI, and the
   same native boundary would still be required. Rejected.

## Tradeoffs
Two native implementations of the same two interfaces, plus channel plumbing.
Contained, because the interfaces are small and stable by design.

## Failure modes
Channel stalls while LIVE → the PC's watchdog ends PTT regardless (ADR-008), so
a wedged Flutter isolate cannot hold the mic open.

## Security implications
Private keys never cross the platform channel; only opaque handles and results
do. The UI layer is never trusted with key material.
