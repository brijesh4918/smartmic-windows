# Using SmartMic away from home (Tailscale)

SmartMic normally needs the phone and the PC on the same Wi-Fi. Tailscale puts
them on the same *virtual* network wherever they are, and SmartMic works over it
unchanged — no code differences, no port forwarding, no exposing anything to the
internet.

It is free for personal use. Nothing here needs a subscription.

## Setup

1. **PC**: install Tailscale from https://tailscale.com/download and sign in.
2. **Phone**: install Tailscale from the App Store / Play Store, sign in with
   the same account.
3. Run the service as usual:

   ```
   smartmic-service.exe --sink driver --port 47820
   ```

   It now prints a **second pairing link** by itself:

   ```
     qr            smartmic://pair?v=1&host=192.168.1.20&...

     Reachable from anywhere via 100.94.12.7 on Tailscale:
       smartmic://pair?v=1&host=100.94.12.7&...
   ```

4. Paste the **second** link into the phone. That is all.

The service finds the Tailscale address itself by enumerating the machine's
interfaces and looking for the carrier-grade NAT range (`100.64.0.0/10`) that
Tailscale allocates from. You do not need to copy an IP by hand.

If you would rather pin it explicitly:

```
smartmic-service.exe --host 100.94.12.7 --port 47820
```

## Windows Firewall

The first time the phone connects from outside the LAN, Windows may prompt to
allow `smartmic-service.exe`. Allow it. If you dismissed the prompt and it now
fails, add the rule by hand from an Administrator prompt:

```
netsh advfirewall firewall add rule name="SmartMic" dir=in action=allow ^
    protocol=UDP localport=47820
```

## Is this safe to expose?

Reasonably, and for two independent reasons.

Tailscale is a private network: only devices signed into your account can reach
the PC at all. It is not a public port.

And SmartMic does not trust the network anyway. Pairing is a PAKE (ADR-012), so
the six-digit code is never transmitted and cannot be brute-forced offline.
Every media and control packet after that is encrypted and authenticated with a
key derived from that pairing, and the service discards anything that does not
authenticate before the audio router ever sees it. An attacker who reached the
port would get nothing but rejected datagrams.

## What to expect over a remote link

Honestly: worse than on your LAN, and how much worse depends on the route.

Tailscale usually establishes a direct peer-to-peer connection, which is fast.
When it cannot — restrictive NATs, some corporate networks — it falls back to
relaying through a DERP server, which adds latency and jitter. The jitter buffer
adapts (it widens its target when arrivals get irregular), so speech stays
intelligible, but push-to-talk will feel less immediate than at home.

The transport in this build is plain UDP with authenticated encryption
(ADR-011). It has no congestion control and no ICE, because it was built for a
LAN. Tailscale makes that LAN bigger rather than fixing those gaps. If remote
use becomes the main way you use SmartMic rather than an occasional
convenience, the WebRTC transport described in ADR-004 is the right answer —
it brings ICE and congestion control, and it drops in behind the same
`ITransport` interface without touching anything above it.
