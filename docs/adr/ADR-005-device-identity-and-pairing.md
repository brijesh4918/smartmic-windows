# ADR-005 — Device Identity and Pairing

**Status:** Accepted · **Date:** 2026-09-12

## Context
A 6-digit code is 20 bits. It is a usable *human* factor and an unusable
*cryptographic* one. The real credential must be a long-term key.

## Decision
Each install generates an Ed25519 identity keypair stored in OS-protected
storage (DPAPI / Android Keystore / iOS Keychain). Pairing runs a **PAKE
(SPAKE2)** over a PSK derived from the one-time code, then both sides sign the
transcript with their long-term keys and persist the peer's public key. The code
expires in 300 s, allows 5 attempts, permits one concurrent pairing session, and
is destroyed on first use. QR additionally carries the PC key fingerprint for
out-of-band MITM protection. All later connections use mutual challenge–response
over the stored keys — never the code.

## Alternatives
1. **Send the code and compare it.** Trivially sniffable and replayable on a
   hostile LAN. Rejected.
2. **Long random pairing string.** Secure, unusable by hand. Rejected as the
   primary path (the QR effectively provides this when scanned).
3. **Cloud account as the root of trust.** Breaks the "no cloud for LAN MVP"
   requirement. Rejected.
4. **TOFU with no code.** Anyone on the LAN could pair during the window.
   Rejected.

## Tradeoffs
A PAKE is more implementation work than a naive exchange and needs a reviewed
library, not a hand roll. That cost is the entire defence against T2/T3.

## Failure modes
Code expires mid-pairing → clear "code expired, generate a new one". Clock skew
→ TTL is enforced on the PC's monotonic clock, not wall time. Key store
unavailable (device not unlocked) → pairing refuses rather than falling back to
weaker storage.

## Security implications
Revocation is local and immediate. Compromise of one phone does not affect other
paired phones. No shared secrets across devices, so no class break.
