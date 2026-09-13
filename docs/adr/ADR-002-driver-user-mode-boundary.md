# ADR-002 — Driver / User-Mode Boundary

**Status:** Accepted · **Date:** 2026-09-12

## Context
Everything interesting — WebRTC, Opus, TLS, pairing, device enumeration,
routing — could technically be done in either mode. Anything done in kernel mode
turns a bug into a bugcheck and an exploit into SYSTEM.

## Decision
The driver does exactly two things: present the endpoint, and drain a bounded
shared PCM ring. Every decision, every parser, every crypto operation, every
allocation of interesting size lives in `SmartMic.AudioService` in user mode.
The interface is a fixed-layout shared ring plus five exact-length
`METHOD_BUFFERED` IOCTLs (see `docs/architecture/05-driver-interface.md`).

## Alternatives
1. **Network stack in kernel** (kernel sockets, WSK). Absurd attack surface for
   a consumer audio product. Rejected.
2. **Opus decode in kernel** to avoid a copy. Saves microseconds, adds a codec
   parser at ring 0. Rejected.
3. **Inverted call model** (driver calls up into a user-mode service per buffer).
   Couples the driver's real-time path to user-mode scheduling; a hung service
   becomes a hung audio stack. Rejected in favour of the lock-free ring.

## Tradeoffs
One extra memory copy per buffer, and the ring's bounded latency (~100 ms of
capacity, a few ms of working fill). In exchange, the kernel component is small
enough to audit fully and to fuzz exhaustively.

## Failure modes
Service stops writing → driver sees a stale `producerHeartbeat` → silence.
Service writes too fast → overrun, oldest dropped, counter incremented; audio
stays current rather than drifting late. Ring version mismatch after an upgrade
→ service refuses to map and reports a repair-needed state.

## Security implications
The driver accepts only fixed-size structures, takes a single-client claim, and
treats every index as untrusted (masked into range). No kernel allocation is
sized from user input. This is what makes T8 in the threat model tractable.
