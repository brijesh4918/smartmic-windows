# 2. Audio Data Flow

## 2.1 Steady state (LOCAL_ACTIVE)

```
Realtek mic ──WASAPI shared, event-driven──► [format conv] ──► [resample→48k mono]
                                                                      │
                                                             ┌────────▼────────┐
                                                             │  AudioRouter    │
   (phone path idle, jitter buffer drained & parked)         │  gain · fade    │
                                                             └────────┬────────┘
                                                                      │ 48k/mono/f32
                                                            [→ s16 or float mix fmt]
                                                                      ▼
                                                        DriverLink SPSC ring (bounded)
                                                                      ▼
                                                   SmartMic.Driver WaveRT capture buffer
                                                                      ▼
                                                                 Teams / Zoom
```

## 2.2 PTT active (PHONE_ACTIVE)

```
Phone mic ─AudioRecord/AVAudioEngine─► 20 ms frames ─Opus─► WebRTC/SRTP ─► LAN
                                                                            │
                                              ┌─────────────────────────────▼──┐
                                              │ PhoneMediaReceiver → Opus dec  │
                                              │ JitterBuffer (adaptive, 40ms)  │
                                              └─────────────────┬──────────────┘
 Realtek mic ──► (still captured, faded to 0, kept warm) ───────┤
                                                        ┌───────▼────────┐
                                                        │  AudioRouter   │
                                                        └───────┬────────┘
                                                         same sink as above
```

The local capture stream is **never stopped** during PTT. It is only faded to
zero gain. Stopping/restarting a WASAPI client costs tens of ms and can fail on
resume; keeping it warm makes failback instant and is the reason the failback
budget can be stated in single-digit milliseconds of audio.

## 2.3 Canonical representation

| Property | Value | Why |
|---|---|---|
| Sample rate | 48 000 Hz | Opus native; WASAPI mix format on essentially all modern Windows audio |
| Channels | 1 (mono) | The product is a microphone. Downmix at the edge, once. |
| Sample type | `float32` interleaved (trivially mono) | headroom during crossfade/gain; no intermediate clipping |
| Frame | 960 samples = 20 ms | Opus frame size; WASAPI 10 ms periods aggregate cleanly into it |
| Conversion to driver | f32 → PCM16 with TPDF dither, hard-limited at ±0.999 | driver stays dumb |

Everything converts to canonical **once, at the source boundary**. The router
never sees a rate or channel count other than 48 k mono.

## 2.4 Latency budget (targets, to be measured — see §16 of the brief)

| Stage | Target |
|---|---|
| Phone capture + Opus encode | 20–30 ms |
| LAN transit | 2–10 ms |
| Jitter buffer (adaptive) | 20–60 ms |
| Opus decode + resample | < 2 ms |
| Router | < 1 ms (pure arithmetic) |
| DriverLink ring + WaveRT | 10–20 ms |
| **Total phone→app** | **~60–120 ms** |
| Local mic → app (no PTT) | 15–30 ms |
