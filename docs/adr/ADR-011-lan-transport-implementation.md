# ADR-011 — LAN Transport Implementation (supersedes part of ADR-004)

**Status:** Accepted · **Date:** 2026-09-12

## Context

ADR-004 chose WebRTC for phone→PC media, and that remains right for the shipped
product: it brings DTLS-SRTP, ICE, congestion control, NACK/PLC and the only
credible path to Phase 9 remote mode.

It is also a very heavy dependency to build on three platforms, and it cannot be
built or exercised on the machine this work is being done on. Blocking every
other Phase 2/3 component behind it would mean the jitter buffer, the protocol,
the pairing exchange, the failback behaviour and the end-to-end audio path all
go unverified until a Windows box and three mobile toolchains are available at
once — which is exactly the "debug everything at the end" failure the phased
plan exists to avoid.

## Decision

Define `ITransport` as the single seam for all phone↔PC media and control, and
ship **two** implementations behind it:

1. `SodiumUdpTransport` — the Phase 2/3 proving vehicle. Plain UDP with
   libsodium `crypto_secretbox` (XSalsa20-Poly1305) AEAD framing, keyed by the
   session key from the pairing exchange, with an explicit 64-bit nonce
   counter and a sliding replay window. Builds and runs anywhere, including CI.
2. `WebRtcTransport` — the production implementation, dropped in at Phase 5/9
   with no change above the interface.

The canonical audio payload (Opus, 48 kHz mono, 20 ms) and every control
message are identical across both. Only the framing and the key exchange to the
media layer differ.

## Alternatives

1. **WebRTC only, from the start.** Correct end state, but leaves the entire
   Phase 2/3 surface untested until the heaviest dependency is working on every
   platform simultaneously. Rejected as a sequencing decision, not a technical
   one.
2. **Plaintext UDP for development.** Would make the "no raw audio on the wire"
   invariant (T4) untestable and would let an insecure default survive into a
   demo. Rejected: the dev transport is encrypted and authenticated too.
3. **Hand-rolled DTLS.** No. The point of libsodium here is to use one
   well-reviewed AEAD in the most boring way possible.

## Tradeoffs

`SodiumUdpTransport` has no ICE, no congestion control and no NACK, so it works
only on a LAN with a direct route — which is precisely the MVP's stated scope.
It buys the ability to test loss, jitter, reorder, duplication and failback
today, against the real state machine, with real Opus audio.

The risk this introduces is that `ITransport` accidentally gets shaped around
the simple transport and then fights WebRTC later. Mitigated by keeping the
interface packet-oriented and asynchronous (`send(bytes)` / `onPacket`), with no
synchronous reads and no assumption of ordering or delivery — the properties
WebRTC also does not offer.

## Failure modes

Dev transport used in production by mistake → the service refuses to start a
`SodiumUdpTransport` session unless built with `SMARTMIC_ALLOW_DEV_TRANSPORT`,
which release builds do not define.

## Security implications

The dev transport is authenticated and encrypted with a key that only a paired
peer can derive, so it does not weaken T1/T4/T5 relative to the production path.
It does lack WebRTC's forward secrecy at the media layer beyond the per-session
key, which is acceptable for a LAN development vehicle and is why it is not a
shipping configuration.
