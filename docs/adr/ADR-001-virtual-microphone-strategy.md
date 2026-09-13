# ADR-001 — Virtual Microphone Strategy

**Status:** Accepted · **Date:** 2026-09-12

## Context
Windows applications must see one microphone whose identity never changes, while
the audio behind it alternates between a physical mic and a phone. Applications
like Teams and Zoom bind to an endpoint at call start; an endpoint that appears,
disappears or changes identity mid-call is either ignored or causes a visible
device-change event.

## Decision
Ship a kernel-mode WDM/WaveRT **virtual audio capture endpoint**, derived from
Microsoft's SysVAD sample, named "Smart Microphone". Its format is fixed for the
life of the endpoint (48 kHz, mono, PCM16). Source selection happens entirely in
user mode, invisibly to the endpoint's consumers.

## Alternatives
1. **Switch the Windows default endpoint per PTT.** Requires undocumented APIs
   for reliable programmatic switching, does nothing for apps that bound to a
   specific device, and visibly disrupts running calls. Rejected.
2. **Audio Processing Object (APO).** APOs sit in an existing device's pipeline
   and cannot originate audio from a different source; also tied to OEM driver
   packages, which the brief forbids touching. Rejected.
3. **Third-party virtual cable (VB-Cable etc.) as the product.** Licensing,
   installer ownership, support burden, and a second brand in the user's device
   list. Accepted as a *development* stand-in only (ADR-002).
4. **User-mode-only virtual device.** Windows has no supported mechanism for a
   user-mode process to publish a capture endpoint. Rejected.

## Tradeoffs
Gains a stable endpoint and full control. Costs: kernel development, Driver
Verifier/HLK work, EV certificate, Partner Center attestation, and a driver
crash being a bugcheck. This is the single largest cost in the project and is
deliberately deferred to Phase 5.

## Failure modes
Driver fails to load → no endpoint → the desktop app must detect this and say so
plainly rather than silently doing nothing. Service dies → endpoint remains and
emits silence (ADR-008). Format mismatch with a picky app → mitigated by never
renegotiating format.

## Security implications
Kernel code is the highest-value attack surface in the product; ADR-002
constrains it to the minimum possible. Unsigned drivers are never shipped.
