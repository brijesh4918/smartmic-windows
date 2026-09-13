# 5. Windows Driver / User-Mode Interface Proposal

## 5.1 Shape

Kernel side is a SysVAD-derived WaveRT capture miniport exposing one endpoint,
"Smart Microphone". User side is `SmartMic.AudioService`. They share exactly two
things:

1. A **control channel**: a handful of IOCTLs on a device interface.
2. A **data channel**: one bounded, shared, single-producer/single-consumer ring
   of PCM, mapped into both address spaces.

No other coupling. The driver never calls into user mode; user mode never blocks
on the driver.

## 5.2 Data channel

```c
// Shared page layout. Fixed size, allocated by the driver, mapped read/write
// into the service process. Header is cache-line separated.
#define SMARTMIC_RING_MAGIC   0x534D5231u   // 'SMR1'
#define SMARTMIC_RING_VERSION 1u

typedef struct _SMARTMIC_RING_HEADER {
    ULONG   magic;             // SMARTMIC_RING_MAGIC
    ULONG   version;
    ULONG   capacityFrames;    // power of two
    ULONG   channels;          // 1
    ULONG   sampleRate;        // 48000
    ULONG   bytesPerFrame;     // 2  (PCM16 mono)
    ULONG   _pad0[10];
    // --- producer cache line (user mode writes) ---
    volatile ULONGLONG writeIndex;   // monotonic, frames
    volatile ULONGLONG producerHeartbeat; // 100ns ticks, service liveness
    ULONGLONG _pad1[6];
    // --- consumer cache line (kernel writes) ---
    volatile ULONGLONG readIndex;    // monotonic, frames
    volatile ULONG     underrunCount;
    volatile ULONG     overrunCount;
    ULONGLONG _pad2[6];
} SMARTMIC_RING_HEADER;
// followed by capacityFrames * bytesPerFrame of PCM
```

- `capacityFrames` = 4800 (100 ms). Power of two in the implementation (8192)
  so index wrapping is a mask, never a division.
- Indices are monotonic 64-bit counters; `available = write - read`. This
  removes the classic full/empty ambiguity without a separate flag.
- Ordering: producer writes payload, then `_ReleaseStore` on `writeIndex`.
  Consumer `_AcquireLoad`s `writeIndex` before reading payload. On x64 this is
  a compiler barrier plus a plain store; on ARM64 it is `stlr`/`ldar`.
- **Starvation rule:** if `available < requested`, the driver emits silence for
  the shortfall, bumps `underrunCount`, and *advances `readIndex` past the gap*.
  It never stalls, never spins, never partially fills without advancing.
- **Staleness rule:** if `producerHeartbeat` has not advanced within 250 ms, the
  driver treats the ring as dead and emits pure silence until it moves again.
  This is what makes "service crashed" indistinguishable from "quiet room" to
  Teams, which is exactly the desired behaviour.

## 5.3 Control channel (IOCTLs)

| IOCTL | Direction | Payload | Notes |
|---|---|---|---|
| `IOCTL_SMARTMIC_GET_VERSION` | out | `{driverVersion, ringVersion}` | first call; refuse everything else on mismatch |
| `IOCTL_SMARTMIC_MAP_RING` | out | ring handle/section | one client at a time, enforced by a driver-held claim |
| `IOCTL_SMARTMIC_UNMAP_RING` | — | — | idempotent |
| `IOCTL_SMARTMIC_GET_STATS` | out | `{underrun, overrun, streamOpen, framesDelivered}` | diagnostics only |
| `IOCTL_SMARTMIC_SET_FORMAT_HINT` | in | `{rate, channels}` | validated against a fixed allowlist; rejected while a stream is open |

Validation rules for every IOCTL: `METHOD_BUFFERED`, exact expected input and
output length (`==`, not `>=`), `FILE_WRITE_ACCESS` required for anything that
mutates, and a single-client claim taken with an interlocked compare-exchange.
No IOCTL allocates memory whose size is derived from user input.

## 5.4 What lives on which side

| Concern | Kernel | User |
|---|---|---|
| Endpoint identity, WaveRT plumbing | ✔ | |
| Ring drain, silence-on-starve | ✔ | |
| Buffer/IOCTL validation | ✔ | |
| Opus, WebRTC, TLS, PAKE | | ✔ |
| Device enumeration, routing, crossfade | | ✔ |
| Any decision that could be wrong | | ✔ |

The design goal is that a bug in the interesting half of the product is a
user-mode fault, not a bugcheck.

## 5.5 Development stand-in

Phase 1 does not build this driver. `IDriverLink` has two implementations:
`VirtualCableDriverLink` (renders to a third-party virtual cable via WASAPI —
development only) and, from Phase 5, `SmartMicDriverLink` (the ring above).
The router is unaware of which is in use.
