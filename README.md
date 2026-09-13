# SmartMic

A phone that can temporarily become the microphone for one of several paired
Windows PCs — without any application ever having to change its microphone.

Windows applications see one stable endpoint, **Smart Microphone**. What sits
behind it changes; the endpoint does not.

```
Realtek mic ─┐
             ├─► AudioRouter ─► Smart Microphone ─► Teams / Zoom / Chrome
Phone (PTT) ─┘
```

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Architecture, ADRs, protocol, threat model | done — `docs/` |
| 1 | Audio router, crossfade, state machine, dev virtual cable | done |
| 2 | Opus, jitter buffer, transport, end-to-end LAN audio | done, runs on this machine |
| 3 | Pairing (CPace), device identity, authenticated sessions | done, 72 tests total |
| 4 | Flutter app, native phone core (AAudio/CoreAudio, FFI) | **APK builds** — `dist/` |
| 5 | SmartMic.Driver (PortCls/WaveRT) + user-mode driver link | **written, never compiled** |
| 6 | **Installer, WinUI desktop app, discovery (mDNS)** | **not started** |
| 7–9 | Security review, HLK/signing, remote mode | not started |

The whole system **except** the Windows kernel driver, WASAPI capture, the mobile
UIs and mDNS discovery runs and is tested here. What is missing is listed
honestly in `docs/architecture/13-gaps.md`.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

`smartmic_core` is platform-neutral by design (ADR-002/003): the router, state
machine, ring buffer, crossfade and resampler build and are tested on clang,
gcc and MSVC with no OS audio dependency. Only `platform/windows/` needs the
Windows SDK.

### The audible artifact

```bash
./build/windows/audio-service/smartmic-router demo out.wav
```

Renders an 8-second scripted scenario — local mic, PTT to phone, release, PTT
again followed by the phone dying mid-stream, then Mute → Local Only → Auto.
Listen for clicks at the transitions; there should be none.
`tests/audio-fixtures/phase1-switching-demo.wav` is a checked-in rendering.

### The end-to-end demo (no Windows needed)

Two processes, real UDP, real pairing, real Opus, real encryption:

```bash
./build/apps/smartmic-service --port 47820 --out out.wav --duration 11 \
    --code 731482 --session demo &
./build/apps/smartmic-phone --host 127.0.0.1 --port 47820 \
    --session demo --code 731482 --hold-after 1.5 --hold-for 2 --presses 2 --gap 1.5 --duration 9
```

`out.wav` is the SmartMic endpoint's output: 317 Hz while the local microphone
is the source, 1093 Hz while the phone holds PTT, with the crossfade at each
switch. Add `--die-during-ptt` to the phone to watch the watchdog fail back.

Omit `--code`/`--session` and the service prints a random code and a QR URI,
which the phone can consume with `--uri 'smartmic://pair?...'`.

### On Windows

```
smartmic-router list
smartmic-router run --in "{capture-device-id}" --out "{virtual-cable-render-id}"
```

Drives a real microphone through the router into a development virtual cable,
with PTT from stdin. A third-party cable is a **development stand-in only** and
is replaced by `SmartMic.Driver` in Phase 5 (ADR-001).

## Where to start reading

- `docs/architecture/01-components.md` — what exists and why
- `docs/architecture/05-driver-interface.md` — the kernel/user boundary
- `docs/security/threat-model.md` — what this thing is, viewed as a weapon
- `docs/adr/` — the ten decisions, with the alternatives that were rejected
- `docs/architecture/12-risks.md` — what is most likely to go wrong

## The two invariants worth knowing

1. **SmartMic can never select its own endpoint as an input.** The installer
   records the pre-install default capture device; the registry filters the
   SmartMic endpoint out of the candidate list; the registry refuses it again if
   asked directly. Three guards, one test (`A12`).
2. **The router can never remain stuck on a dead phone.** The watchdog runs on
   the audio clock, so it keeps ticking even if every network thread is wedged,
   and the local microphone is kept warm throughout PTT so failback costs one
   15 ms fade (`A10`, ADR-008).
