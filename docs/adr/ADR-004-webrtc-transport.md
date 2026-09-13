# ADR-004 — WebRTC Transport with Opus

**Status:** Accepted · **Date:** 2026-09-12

## Context
Phone→PC voice over Wi-Fi needs low latency, encryption, loss concealment,
NAT traversal (later), and three platform implementations.

## Decision
WebRTC (`PeerConnection`, DTLS-SRTP) carrying Opus at 48 kHz mono, 20 ms frames,
~24–32 kbps, with FEC enabled. The PC's media direction is `recvonly`; there is
no PC→phone audio track at all. Control messages travel on a separate
TLS 1.3 TCP channel, not on a data channel, so control survives media teardown.

## Alternatives
1. **Raw UDP + custom framing + custom crypto.** Lower dependency weight; but
   re-implements SRTP, jitter handling, PLC and congestion control. Rejected.
2. **WebSocket/TCP audio.** Head-of-line blocking turns one lost packet into a
   audible stall. Rejected.
3. **RTP without WebRTC.** Loses ICE and the ready-made path to Phase 9 remote
   mode. Rejected.
4. **Control over the WebRTC data channel.** Couples control liveness to media
   liveness — precisely the coupling the failback design needs to avoid.
   Rejected.

## Tradeoffs
WebRTC is a heavy native dependency to build and update on three platforms
(R9). Isolated behind an `ITransport` interface so it is replaceable.

## Failure modes
ICE fails on isolated-client Wi-Fi → surfaced as a clear error, not a retry
loop. Media flows but control dies → watchdog ends PTT. Control flows but media
never arrives → `PHONE_ARMING` times out at 1500 ms and the phone shows FAILED.

## Security implications
DTLS-SRTP gives T4 for free. The DTLS fingerprint is bound into the
authenticated control session so media and control cannot be split.
