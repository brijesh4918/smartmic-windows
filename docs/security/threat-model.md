# 7. Threat Model

Method: STRIDE over the four trust boundaries (phone↔LAN, LAN↔service,
service↔driver, user↔service config).

## 7.1 Assets

| Asset | Why it matters |
|---|---|
| Live microphone audio (phone and PC) | the entire product is an eavesdropping primitive if misused |
| The *ability to inject* audio into the user's calls | impersonation into Teams/Zoom — arguably worse than eavesdropping |
| Device long-term private keys | grants the above, persistently |
| Pairing code (transient) | grants the above, once |
| Kernel-mode driver | a bug here is a system-wide privilege escalation |

## 7.2 Adversaries

- **A1 Same-LAN unauthenticated attacker** (coffee shop, office, guest Wi-Fi). Primary.
- **A2 Same-LAN active MITM** (ARP/DNS spoof, rogue AP).
- **A3 Malicious/compromised local app** on the PC, running as the user.
- **A4 Physical attacker with brief phone access.**
- **A5 Malicious peer** — a phone that paired legitimately and later turns hostile.
- **A6 Network observer** off-LAN (only relevant once remote mode exists).

## 7.3 Threats and mitigations

| # | Threat | Adversary | STRIDE | Mitigation | Residual |
|---|---|---|---|---|---|
| T1 | Unpaired phone injects audio into a live call | A1 | S,T | Mutual public-key auth on every connection; unauthenticated sockets get nothing but `HELLO`/`ERROR`; router refuses phone audio without an authenticated session | none material |
| T2 | Brute-force the 6-digit code | A1 | S | PAKE (code never on the wire, no offline attack) + 5 attempts + 300 s TTL + one concurrent pairing session + code destroyed on first use. ~5/10⁶ per window | negligible |
| T3 | MITM during pairing | A2 | S,I | PAKE binds the channel; QR carries the PC key fingerprint for out-of-band verification | manual-code path has no OOB channel; capped by T2 limits |
| T4 | Passive capture of voice on the LAN | A1,A6 | I | DTLS-SRTP for media, TLS 1.3 for control. No plaintext audio path exists, even on LAN | none |
| T5 | Replay of `START_PTT` to open the mic later | A1,A2 | T,E | Per-session monotonic `seq` + session key + timestamp window; `sessionId` dies with the connection | none |
| T6 | Stuck-open microphone (phone crashes mid-PTT) | — (safety) | D | Heartbeat watchdog driven off the audio clock; hard failback at 600 ms; no unbounded wait exists in the state machine | ≤600 ms of phone audio after death |
| T7 | Local app drives the service's control pipe to hijack routing | A3 | E,T | Named pipe ACL restricted to the interactive user + Administrators; no remote endpoint; mutating ops require the desktop app's session | A3 already runs as the user — accepted |
| T8 | Malformed IOCTL / ring indices bugcheck the box | A3 | D,E | Exact-length buffered IOCTLs, single-client claim, allowlisted formats, indices treated as untrusted and masked into range, driver never allocates from user-supplied sizes | requires fuzzing + Driver Verifier (Phase 5/7 gate) |
| T9 | Service starves the driver and the mic goes noisy | — | D | Silence-on-starve + producer heartbeat staleness detector | audio simply goes quiet |
| T10 | Stolen phone retains access | A4 | S | PC-side `Forget` revokes immediately and locally; keys in Keystore/Keychain with device-unlock protection | no remote wipe in MVP |
| T11 | Paired phone silently records the PC's room | A5 | I | SmartMic never streams PC→phone. The media path is strictly one-way, enforced by the SDP direction (`recvonly` at the PC) | none by construction |
| T12 | Audio persisted to disk or logs | A3, ops | I | No recording by default; logging layer has no API that accepts sample buffers; diagnostics bundle is an allowlist of fields | discipline enforced by code review + a test asserting no audio in log sinks |
| T13 | Discovery leaks who/where the user is | A1 | I | TXT records carry only: protocol version, a rotating PC instance id, driver-ready flag. No user name, no key material, no phone list | PC friendly name is optional and off by default |
| T14 | Unsigned/tampered driver | A3 | T,E | Production driver is Microsoft-attested/HLK-signed; installer verifies the catalog; test-signed builds are dev-only and gated behind an explicit flag | — |
| T15 | Downgrade to an older protocol version | A2 | T | `protocolVersion` is in the signed pairing/auth transcript; mismatched versions fail closed rather than negotiate down | — |

## 7.4 Non-goals for MVP

Remote (off-LAN) operation, multi-user PCs with per-user routing, anti-forensic
guarantees, defence against a fully compromised Windows kernel, and defence
against a user who deliberately pairs an attacker's phone.

## 7.5 Security invariants (testable)

1. No audio frame reaches `AudioRouter` from a session that is not `AUTHENTICATED`.
2. No log sink is reachable with a pointer to sample data.
3. A pairing code is usable exactly once.
4. `seq` never goes backwards within a session.
5. The driver emits silence — never stale, never garbage — when starved.
