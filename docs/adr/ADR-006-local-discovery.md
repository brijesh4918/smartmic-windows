# ADR-006 — Local Discovery

**Status:** Accepted · **Date:** 2026-09-12

## Context
The phone must find PCs on the same network with zero configuration, on two
mobile OSes, without a cloud account.

## Decision
The Windows service advertises `_smartmic._tcp` over mDNS/DNS-SD. Android uses
`NsdManager`; iOS uses Network Framework (`NWBrowser`/`NWListener`) with the
required `NSBonjourServices` and Local Network usage declarations. TXT records
carry only: `v` (protocol version), `id` (rotating instance id), `drv` (driver
ready 0/1). Never a user name, never key material, never a paired-device list.
QR/manual entry carries host+port directly as the fallback when multicast is
blocked.

## Alternatives
1. **UDP broadcast.** No service model, blocked at least as often, and each OS
   needs custom code anyway. Rejected.
2. **Cloud presence for LAN.** Violates the no-account MVP requirement and adds
   an availability dependency to a local feature. Rejected.
3. **Manual IP entry only.** Works everywhere, terrible first-run experience.
   Kept as the documented fallback, not the default.

## Tradeoffs
mDNS is unreliable on enterprise Wi-Fi with multicast filtering or client
isolation (R7). The QR fallback covers multicast filtering; client isolation is
unsolvable on-LAN and is honestly reported as such.

## Failure modes
Discovery finds nothing → UI offers QR/manual immediately rather than spinning.
Stale advertisement after an IP change → the phone reconnects by name and
re-resolves; a failed resolve drops the cached address.

## Security implications
Discovery is unauthenticated by nature, so it advertises nothing sensitive
(T13). Being discovered grants no capability: an unpaired peer can obtain only
`HELLO` and `ERROR`.
