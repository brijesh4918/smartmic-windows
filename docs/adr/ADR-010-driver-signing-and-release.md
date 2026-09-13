# ADR-010 — Driver Signing and Release Strategy

**Status:** Accepted · **Date:** 2026-09-12

## Context
A kernel driver cannot load on 64-bit Windows without a signature Microsoft
trusts. Obtaining that is an identity, procurement and process problem with
weeks of calendar latency, and it gates shipping entirely (R1).

## Decision
Two strictly separate workflows.

**Development:** WDK test-signing, `bcdedit /set testsigning on` on a dedicated
test machine or VM — never on a developer's daily driver. Driver Verifier
enabled with standard + DDI compliance + low-resources simulation. Automated
install/upgrade/uninstall cycling in CI on that machine.

**Production:** an EV code-signing certificate held by the company, a Partner
Center (Hardware Developer) account bound to it, HLK/WHCP runs for the applicable
audio playlists, submission through the Windows Hardware Developer process, and
distribution of the Microsoft-signed catalog. No unsigned or self-signed kernel
binary is ever distributed, to anyone, for any reason — including beta testers.

Procurement of the EV certificate and the Partner Center account starts in
Phase 0, not Phase 8.

## Alternatives
1. **Ask users to enable test signing.** Disables a core Windows security
   boundary on their machine. Never acceptable for a shipped product. Rejected.
2. **Cross-signed certificate.** No longer a valid path for new kernel drivers
   on current Windows. Rejected.
3. **Avoid kernel mode entirely.** Would mean no stable endpoint (ADR-001) or
   depending on a third-party cable in production, which the brief forbids.
   Rejected.

## Tradeoffs
High fixed cost and calendar risk, in exchange for the only architecture that
actually delivers the product requirement.

## Failure modes
Attestation rejects the submission → the driver must already be minimal and
HLK-clean, which ADR-002 supports. Certificate expiry → renewal tracked as a
release-engineering task with a calendar alert, not discovered at ship time.

## Security implications
The signing key is the crown jewel: hardware token only, two-person control for
release signing, and no CI runner ever holds it.
