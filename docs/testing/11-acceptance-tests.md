# 11. Acceptance Tests

## 11.1 Phase 1 gate (must all pass before Phase 2 starts)

### Automated — portable, run on any dev machine and in CI

All of these are implemented in `windows/audio-service/tests/` and pass
(31 tests). Run with `ctest --test-dir build` or `./build/windows/audio-service/smartmic_tests`.

| ID | Test | Pass criterion |
|---|---|---|
| A1 | Ring buffer FIFO ordering | every value read back in order |
| A2 | Ring buffer overrun | oldest dropped, `overrunCount` exact, no corruption |
| A3 | Ring buffer underrun | short read reports exact count, no blocking |
| A4 | Ring buffer threaded soak, 1 M frames | zero loss when producer ≤ consumer; no torn frames |
| A5 | Crossfade energy | equal-power: `a²+b²` within 1 % of unity across the curve |
| A6 | Crossfade continuity | max |Δsample| through the transition ≤ 1.15× the steady-state maximum of either source alone, for two non-harmonic tones with a phase offset |
| A6c | Negative control for A6 | with the crossfade disabled, A6's measurement **must** fail (guards against the test silently going blind) |
| A7 | Crossfade sample-accuracy | fade completes in exactly `ceil(ms·48)` samples; a mid-fade retarget does not jump |
| A8 | Resampler | 44.1 k→48 k sine keeps frequency within 0.1 %, no gap across block boundaries |
| A9 | State machine legality | all 8 states; every illegal edge rejected without side effects |
| A10 | No stuck PHONE_ACTIVE | with no heartbeat, router reaches LOCAL_ACTIVE within 600 ms of audio time |
| A11 | Idempotent stop | `STOP_PTT` ×3 from any state ends in LOCAL_ACTIVE, no error |
| A12 | Self-selection rejected | registry hides the SmartMic endpoint; router refuses it explicitly |
| A13 | Failback on source death | source returning error → local/silence, router keeps producing frames |
| A14 | Continuity of output | router emits exactly one 20 ms frame per tick across every transition — never a gap |
| A15 | No clipping | full-scale on both sources, output stays within ±1.0 |
| A15c | Gate smoothness | entering and leaving Mute steps no more than a crossfade does |

### Manual — requires a Windows box (step 1.6/1.7)

| ID | Test | Pass criterion |
|---|---|---|
| M1 | Endpoint stability | source switched 50× during a 10-min Teams call; Teams never re-selects a device, never shows a device-change toast |
| M2 | Inaudible switch | 3 listeners cannot identify the switch instant in a blind recording |
| M3 | App matrix | Teams, Zoom, Discord, Chrome (`getUserMedia`), Edge, Voice Recorder all capture correctly and survive ≥10 switches |
| M4 | Hot-plug | USB headset in/out while running: registry updates, router keeps producing, optional auto-prefer works |
| M5 | Sleep/resume | S3 and modern standby: audio resumes within 5 s with no restart of the app's stream |
| M6 | Source death | unplug the active mic mid-stream → silence or fallback, never a crash, never a stall |
| M7 | Soak | 8 h continuous with a switch every 30 s: zero underruns attributable to the router, RSS flat within 5 % |

## 11.2 Later-phase gates (recorded now so they are not invented under pressure)

- **Phase 2:** phone→PC audio is intelligible over real Wi-Fi; `PTT_READY` precedes `LIVE` in 100 % of 500 presses; median press→LIVE < 400 ms.
- **Phase 3:** unauthenticated phone cannot inject a single audio frame (asserted at the router, not just the socket); revocation takes effect on the next connection attempt with no restart.
- **Phase 5:** driver survives Driver Verifier (standard + DDI compliance + low resources) through 1 000 install/uninstall cycles; killing the service mid-call yields silence in Teams, not noise, not a hang.
- **Phase 7:** at 5 % packet loss, speech remains intelligible and the system never remains in `PHONE_ACTIVE` after the phone is powered off — asserted over 200 induced failures.
