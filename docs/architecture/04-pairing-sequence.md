# 4. Pairing Sequence

```
   PC (Desktop app)                                    Phone
        │                                                │
   user clicks "Pair phone"                              │
        │                                                │
   generate pairingSessionId                             │
   generate 6-digit code  C  (CSPRNG, 20 bits)           │
   derive  PSK = HKDF(C ‖ pairingSessionId ‖ pcPubKey)   │
   TTL 300 s, max 5 attempts, 1 concurrent session       │
        │                                                │
   show code + QR:                                       │
     smartmic://pair?v=1&host=192.168.1.20&port=47820    │
       &pcid=<b64 pcDeviceId>&fp=<SHA256 pcPubKey>       │
       &code=<C>&exp=<unix>                              │
        │◄──────────── scan QR, or type C ───────────────│
        │                                                │
        │◄────── TCP connect (or mDNS-resolved host) ────│
        │                                                │
        │◄─── HELLO{v, phoneDeviceId, phonePubKey} ──────│
        │──── HELLO{v, pcDeviceId, pcPubKey} ───────────►│
        │                                                │
        │     ===== SPAKE2 / PAKE over PSK =====         │
        │  (code proves possession; never sent on wire)  │
        │◄──────────── PAKE msg A ───────────────────────│
        │───────────── PAKE msg B ──────────────────────►│
        │        both derive session key K               │
        │                                                │
        │◄─ AUTH{ Sign_phone(transcript ‖ K) } ──────────│
        │── AUTH{ Sign_pc   (transcript ‖ K) } ─────────►│
        │                                                │
   verify phone signature                           verify PC signature
   check fingerprint == QR fp (MITM guard)               │
        │                                                │
   persist phonePubKey (DPAPI)                  persist pcPubKey (Keystore/Keychain)
   invalidate code C immediately                         │
        │                                                │
        │──── CAPABILITIES{driverReady, mics, modes} ───►│
        │                                                │
   Desktop shows "Phone paired"                  Phone shows PC in Computers
```

## Subsequent connections

No code. Mutual challenge–response over the stored long-term keys, producing a
fresh session key and `sessionId`; the session key protects the control channel
and is bound into the WebRTC DTLS fingerprint exchange so media and control
cannot be split by an attacker.

## Rules

- The 6-digit code authenticates **one** pairing session and is destroyed on
  first use, on expiry, or after 5 failed attempts — whichever is first.
- A PAKE is used specifically so that a 20-bit secret is not brute-forceable
  offline; the only way to attack it is online, which the attempt limiter caps.
- The QR carries the PC public-key fingerprint, so a QR-paired phone is immune
  to an active MITM even on a hostile LAN. Manual-code pairing relies on the
  PAKE alone, which is sufficient but weaker against sustained online attack —
  hence the attempt cap.
- `Forget` on either side deletes the peer key locally and, if the peer is
  reachable, sends a revocation notice. Revocation is authoritative locally; it
  does not depend on delivery.
