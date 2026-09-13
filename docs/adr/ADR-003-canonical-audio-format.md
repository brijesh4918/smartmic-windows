# ADR-003 — Canonical Audio Format

**Status:** Accepted · **Date:** 2026-09-12

## Context
Sources arrive at 16/44.1/48/96 kHz, mono or multi-channel, int16 or float32.
Mixing conversion logic into the router would make every transition a
special case.

## Decision
One canonical representation inside the service: **48 000 Hz, 1 channel,
float32, 960-sample (20 ms) frames**. Every source converts to it at its own
boundary; the router only ever sees canonical frames; the sink converts once to
whatever the driver/device wants (PCM16 with dither for the driver).

## Alternatives
1. **16 kHz mono** — smaller, but audibly worse and requires resampling for
   Opus. Rejected.
2. **44.1 kHz** — forces a resample at both the Windows mix format and Opus.
   Rejected.
3. **int16 internally** — crossfade and gain would need saturating arithmetic
   and would lose headroom at the seam. Rejected.
4. **Variable frame size** — would let sources pass through untouched, but makes
   crossfade timing, the jitter buffer and the ring all size-dependent.
   Rejected: fixed 20 ms is what makes A7/A14 checkable.

## Tradeoffs
A resample step for any non-48 k source (cheap, and most hardware is already
48 k). float32 doubles internal memory versus int16 — irrelevant at these sizes.

## Failure modes
A source whose rate changes mid-stream (some USB devices) → the source-side
converter is reinitialised and the router sees only canonical frames, so the
event is invisible above the boundary.

## Security implications
Fixed frame geometry removes a whole class of length-confusion bugs on the path
between the network and the kernel ring.
