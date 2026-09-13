# 3. PTT Sequence

## 3.1 Happy path

```
Phone UI      PhoneNet        ControlServer     AudioRouter      PhoneMedia      Driver
   │             │                  │                │               │             │
 finger DOWN     │                  │                │               │             │
   │──startPTT──►│                  │                │               │             │
   │  state=CONNECTING              │                │               │             │
   │             │──START_PTT(msgId,seq,ts)─────────►│               │             │
   │             │                  │──arm()────────►│               │             │
   │             │                  │           LOCAL_ACTIVE         │             │
   │             │                  │             → PHONE_ARMING     │             │
   │             │                  │                │──open media──►│             │
   │             │◄─PTT_PREPARING───│                │               │             │
   │             │                  │                │               │             │
   │             │══════ WebRTC media starts (SRTP) ═══════════════► │             │
   │             │                  │                │◄─first frame──│             │
   │             │                  │        jitter buffer ≥ prefill │             │
   │             │                  │◄──ready()──────│               │             │
   │             │◄─PTT_READY───────│      PHONE_ARMING→PHONE_ACTIVE │             │
   │◄─ack────────│                  │        crossfade 15 ms ────────┼────────────►│
 HAPTIC + LIVE   │                  │                │               │             │
   │             │                  │                │               │             │
   │──heartbeat every 200 ms───────►│──feed()───────►│               │             │
   │             │                  │                │               │             │
 finger UP       │                  │                │               │             │
   │──stopPTT───►│──STOP_PTT───────►│──release()────►│               │             │
   │ state=RELEASING                │      →RETURNING_TO_LOCAL       │             │
   │             │                  │        crossfade 15 ms ────────┼────────────►│
   │             │◄─PTT_STOPPED─────│      →LOCAL_ACTIVE             │             │
   │◄─ack────────│                  │                │               │             │
 HAPTIC + READY  │                  │                │               │             │
```

**Invariant: the phone never renders LIVE before `PTT_READY`.** `PTT_READY` is
only emitted after the router has actually observed decodable phone audio in the
jitter buffer, not merely after the peer connection reports connected.

## 3.2 Failure: phone dies while PHONE_ACTIVE

```
PHONE_ACTIVE
   │  last heartbeat / media frame at T
   │
   ├── T + 300 ms : no heartbeat, no media  ──► WARN, hold (jitter may absorb)
   ├── T + 600 ms : still nothing           ──► PHONE_LOST
   │                                             │
   │                                    RETURNING_TO_LOCAL (15 ms fade)
   │                                             │
   └────────────────────────────────────────► LOCAL_ACTIVE, session torn down
```

There is no path in the state machine from `PHONE_ACTIVE` that waits
indefinitely. The watchdog tick is driven by the audio callback itself, so it
still runs even if every network thread is wedged.

## 3.3 Failure: STOP_PTT lost, duplicated, or reordered

- `STOP_PTT` is idempotent: applying it in `LOCAL_ACTIVE` is a no-op that still
  returns `PTT_STOPPED`, so the phone's retry always terminates.
- Control messages carry a monotonically increasing `seq` per session; anything
  at or below the last accepted `seq` is dropped (replay + reorder protection).
- If `STOP_PTT` never arrives, the heartbeat timeout in §3.2 ends PTT anyway.
  **Loss of the release message can therefore cost at most ~600 ms of extra open
  microphone, never a stuck session.**
