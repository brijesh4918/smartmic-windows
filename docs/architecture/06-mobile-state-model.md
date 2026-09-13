# 6. Mobile State Model

## 6.1 Connection state (per PC)

```
        UNKNOWN
           │ discovered / known
           ▼
      DISCONNECTED ──────────────┐
           │ connect()           │ backoff expires
           ▼                     │
      CONNECTING ──fail──► BACKOFF (0.5,1,2,4,8,15,30 s ±20% jitter, cap 30 s)
           │ ok                  ▲
           ▼                     │
     AUTHENTICATING ──fail───────┘
           │ ok
           ▼
      CONNECTED ──► (heartbeat 1 s, RTT tracked)
           │ loss / revoked / app background
           ▼
      DISCONNECTED
```

`REVOKED` is terminal for that PC: no backoff, no retry, UI shows "This PC
removed this phone" and offers re-pair.

## 6.2 PTT state (only meaningful while CONNECTED)

```
                    ┌─────────────────────────────────────┐
                    ▼                                     │
  ┌────────┐  down  ┌────────────┐  PTT_READY  ┌──────┐   │
  │ READY  │───────►│ CONNECTING │────────────►│ LIVE │   │
  └────────┘        └────────────┘             └──────┘   │
      ▲                  │  timeout 1500 ms        │ up / cancel
      │                  │  or ERROR               ▼
      │                  ▼                    ┌───────────┐
      │             ┌────────┐                │ RELEASING │
      └─────────────│ FAILED │◄───────────────└───────────┘
        3 s / tap   └────────┘   PTT_STOPPED or 800 ms timeout
```

Rules, all of them load-bearing:

- **`LIVE` is only entered on `PTT_READY` from the PC.** Never on local
  optimism, never on `PeerConnection.connected`.
- Finger-up in *any* state sends `STOP_PTT` (idempotent) and goes to
  `RELEASING`. There is no state in which releasing the button does nothing.
- Every touch-loss event — `onPointerCancel`, app backgrounded,
  `AVAudioSession` interruption, phone call, screen lock, route change to a
  device without a mic — is treated as finger-up.
- `RELEASING` has its own timeout so a lost `PTT_STOPPED` cannot pin the UI.
- Haptic: one crisp impact on `READY→LIVE`, one softer on `→READY`. Nothing on
  `CONNECTING` (it would teach users to trust the wrong moment).
- Visual: `READY` is outline/neutral; `LIVE` is filled, saturated, with a live
  level meter and the PC name. The two must be distinguishable at arm's length
  and by a colour-blind user — so the difference is fill + motion, not hue.

## 6.3 App lifecycle

| Event | Behaviour |
|---|---|
| Backgrounded while `LIVE` | `STOP_PTT` immediately, then keep the control socket briefly (Android foreground service w/ `microphone` type; iOS is allowed only a short tail) |
| Backgrounded while `READY` | control socket closed after 30 s; presence goes offline |
| Foregrounded | re-discover + reconnect active PC, no auto-PTT |
| Audio interruption (call) | `STOP_PTT`, PTT disabled until `.ended` |
