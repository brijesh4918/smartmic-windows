# ADR-008 — Failback Behaviour

**Status:** Accepted · **Date:** 2026-09-12

## Context
The phone will die mid-PTT: Wi-Fi drops, the OS kills the app, the battery goes,
the user walks out of range. The product is unacceptable if any of those leaves
the user's microphone dead in the middle of a meeting.

## Decision
Three layers, each independent of the one above:

1. **Router watchdog.** No phone media or heartbeat for 600 ms while
   `PHONE_ACTIVE` → `RETURNING_TO_LOCAL` → `LOCAL_ACTIVE`, with the normal
   crossfade. Driven by the audio callback, so it survives wedged network
   threads.
2. **Local source kept warm.** The physical capture stream is never stopped
   during PTT, only faded to zero, so failback costs one fade and nothing else.
3. **Driver fails to silence.** If the whole service dies, the endpoint survives
   and emits silence (stale `producerHeartbeat`), so applications see a quiet
   mic rather than noise, garbage or a hang.

## Alternatives
1. **Longer timeout (2–3 s) to ride out Wi-Fi hiccups.** Three seconds of dead
   microphone in a meeting is worse than a brief glitch. Rejected; the jitter
   buffer absorbs real hiccups, and 600 ms is the point past which it is no
   longer a hiccup.
2. **Stop the local stream during PTT to save power.** Restart latency is tens
   of ms and can fail outright after resume. Rejected.
3. **Mix both sources permanently** to sidestep switching. Leaks the phone's
   room into every call. Rejected.

## Tradeoffs
Keeping the local mic open during PTT costs a little CPU and keeps the mic-in-use
indicator lit. Acceptable, and arguably more honest.

## Failure modes
Both sources fail → `ERROR_RECOVERY` emits silence and keeps producing frames on
schedule; the output stream never gaps (test A14).

## Security implications
Failback is a safety property, not a security one, but "never stuck live" also
bounds how long a hijacked session could hold the mic (T6).
