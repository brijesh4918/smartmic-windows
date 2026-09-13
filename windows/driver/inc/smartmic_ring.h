/*++
    smartmic_ring.h

    The ONLY thing shared between SmartMic.Driver (kernel) and
    SmartMic.AudioService (user mode). Included by both, byte-for-byte.

    See docs/architecture/05-driver-interface.md. The rules that matter:

      * Fixed layout, fixed size, no pointers, no variable-length fields.
      * Indices are monotonic 64-bit counters. available = write - read.
        That removes the classic full/empty ambiguity with no extra flag.
      * The producer (user mode) writes payload, THEN releases writeIndex.
        The consumer (kernel) acquires writeIndex, THEN reads payload.
      * The consumer NEVER blocks and NEVER spins. Short data means silence.
      * The consumer treats every index as untrusted and masks it into range.
--*/

#ifndef SMARTMIC_RING_H
#define SMARTMIC_RING_H

#define SMARTMIC_RING_MAGIC    0x534D5231u   /* 'SMR1' */
#define SMARTMIC_RING_VERSION  1u

/* Canonical format, fixed for the life of the endpoint (ADR-003). */
#define SMARTMIC_SAMPLE_RATE   48000u
#define SMARTMIC_CHANNELS      1u
#define SMARTMIC_BITS          16u
#define SMARTMIC_BYTES_PER_FRAME (SMARTMIC_CHANNELS * (SMARTMIC_BITS / 8u))

/* 8192 frames ~= 170 ms at 48 kHz. Power of two so wrapping is a mask. */
#define SMARTMIC_RING_FRAMES   8192u
#define SMARTMIC_RING_MASK     (SMARTMIC_RING_FRAMES - 1u)

/* If the producer heartbeat has not advanced within this long, the service is
   considered dead and the driver emits pure silence. Expressed in 100 ns
   units so the driver can compare it against KeQueryInterruptTime directly. */
#define SMARTMIC_PRODUCER_TIMEOUT_100NS  (250 * 10000LL)   /* 250 ms */

#include <pshpack8.h>

typedef struct _SMARTMIC_RING_HEADER {
    unsigned long magic;            /* SMARTMIC_RING_MAGIC */
    unsigned long version;          /* SMARTMIC_RING_VERSION */
    unsigned long capacityFrames;   /* SMARTMIC_RING_FRAMES */
    unsigned long channels;
    unsigned long sampleRate;
    unsigned long bytesPerFrame;
    unsigned long payloadOffset;    /* bytes from the start of the section */
    unsigned long reserved0[9];

    /* --- producer cache line: written by user mode only --- */
    volatile unsigned __int64 writeIndex;
    volatile unsigned __int64 producerHeartbeat;   /* KeQueryInterruptTime units */
    unsigned __int64 reserved1[6];

    /* --- consumer cache line: written by the driver only --- */
    volatile unsigned __int64 readIndex;
    volatile unsigned long underrunCount;
    volatile unsigned long overrunCount;
    volatile unsigned __int64 framesDelivered;
    volatile unsigned long streamOpen;
    unsigned long reserved2[9];
} SMARTMIC_RING_HEADER;

#include <poppack.h>

/* Header is padded to 256 bytes so the PCM payload starts cache-aligned and
   the two cache lines above never share one. */
#define SMARTMIC_RING_HEADER_BYTES   256
#define SMARTMIC_RING_PAYLOAD_BYTES  (SMARTMIC_RING_FRAMES * SMARTMIC_BYTES_PER_FRAME)
#define SMARTMIC_RING_TOTAL_BYTES    (SMARTMIC_RING_HEADER_BYTES + SMARTMIC_RING_PAYLOAD_BYTES)

/* --- control channel ------------------------------------------------------ */

/* {B0C2A1D4-7E31-4B8C-9A57-2E6F1D3C8A90} */
DEFINE_GUID(GUID_DEVINTERFACE_SMARTMIC,
    0xb0c2a1d4, 0x7e31, 0x4b8c, 0x9a, 0x57, 0x2e, 0x6f, 0x1d, 0x3c, 0x8a, 0x90);

#define SMARTMIC_DEVICE_TYPE  FILE_DEVICE_SOUND

#define IOCTL_SMARTMIC_GET_VERSION \
    CTL_CODE(SMARTMIC_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SMARTMIC_MAP_RING \
    CTL_CODE(SMARTMIC_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SMARTMIC_UNMAP_RING \
    CTL_CODE(SMARTMIC_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_SMARTMIC_GET_STATS \
    CTL_CODE(SMARTMIC_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)

typedef struct _SMARTMIC_VERSION_INFO {
    unsigned long driverVersion;
    unsigned long ringVersion;
    unsigned long ringTotalBytes;
    unsigned long reserved;
} SMARTMIC_VERSION_INFO;

typedef struct _SMARTMIC_MAP_RESULT {
    void*         baseAddress;      /* user-mode VA of the mapped section */
    unsigned long totalBytes;
    unsigned long reserved;
} SMARTMIC_MAP_RESULT;

typedef struct _SMARTMIC_STATS {
    unsigned __int64 framesDelivered;
    unsigned long    underrunCount;
    unsigned long    overrunCount;
    unsigned long    streamOpen;
    unsigned long    reserved;
} SMARTMIC_STATS;

#endif /* SMARTMIC_RING_H */
