# ADR-007 — PTT State Machine

**Status:** Accepted · **Date:** 2026-09-12

## Context
"Hold to talk" looks trivial and is not: the button is on one device, the audio
switch happens on another, and either can fail at any instant. The dangerous
failures are a mic that is open when the user thinks it is closed, and a UI that
says LIVE when it is not.

## Decision
The PC owns the authoritative state machine with eight states: `LOCAL_ACTIVE`,
`PHONE_ARMING`, `PHONE_ACTIVE`, `RETURNING_TO_LOCAL`, `PHONE_ONLY`,
`LOCAL_ONLY`, `MUTED`, `ERROR_RECOVERY`. The phone mirrors a simplified view and
**may only display LIVE after receiving `PTT_READY`**. `PHONE_ARMING` is left
for `PHONE_ACTIVE` only when decodable phone audio is actually present in the
jitter buffer. `START_PTT`/`STOP_PTT` are idempotent. Every transition is driven
by the audio clock, not a wall timer, so the machine keeps advancing even if
every network thread is blocked.

## Alternatives
1. **Phone-authoritative state.** The phone cannot know whether the PC's
   crossfade completed, and a dead phone would leave the PC's state undefined.
   Rejected.
2. **Switch on `START_PTT` immediately.** First ~100 ms of speech lost to an
   unfilled jitter buffer, plus a stuck-open mic if media never arrives.
   Rejected.
3. **Fewer states (on/off).** Cannot express arming, releasing, or the three
   user-selected modes without hidden booleans. Rejected.

## Tradeoffs
The arming round-trip adds latency between finger-down and LIVE (target
< 400 ms median). That is the price of never lying to the user about being live.

## Failure modes
`PHONE_ARMING` never becomes ready → 1500 ms timeout → `ERROR_RECOVERY` →
`LOCAL_ACTIVE`, phone shows FAILED. Duplicate `STOP_PTT` → no-op + ack.
Out-of-order control messages → rejected by the monotonic `seq` window.

## Security implications
Idempotency plus the replay window means a captured `START_PTT` cannot re-open
the microphone later (T5).
