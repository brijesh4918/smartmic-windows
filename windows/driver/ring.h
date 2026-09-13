/*++
    ring.h -- the kernel side of the service->driver PCM ring.

    Allocation, user mapping, and the drain path used by the WaveRT stream.
--*/
#ifndef SMARTMIC_RING_KERNEL_H
#define SMARTMIC_RING_KERNEL_H

#include "common.h"

class CSmartMicRing
{
public:
    CSmartMicRing();
    ~CSmartMicRing();

    NTSTATUS Initialize();
    void     Cleanup();

    /* Maps the ring into the CALLING process. Exactly one client at a time:
       the claim is taken with an interlocked exchange, so a second service
       (an upgrade overlap, a second session) fails cleanly instead of
       corrupting the first one's view (risk R11). */
    NTSTATUS MapForCurrentProcess(_Out_ PVOID* UserAddress, _Out_ ULONG* TotalBytes);
    NTSTATUS UnmapForCurrentProcess();

    /* Called when a file handle closes without an explicit unmap. */
    void ReleaseClaimIfOwner(_In_ PEPROCESS Process);

    /* Drain `Frames` frames into `Destination`.

       Returns the number of REAL frames copied. Any shortfall has already
       been filled with silence, and readIndex has been advanced past the gap:
       this function never stalls, never spins, and never leaves stale audio
       in the destination. */
    ULONG Read(_Out_writes_bytes_(Frames * SMARTMIC_BYTES_PER_FRAME) PVOID Destination,
               _In_ ULONG Frames);

    void SetStreamOpen(_In_ BOOLEAN Open);
    void GetStats(_Out_ SMARTMIC_STATS* Stats);

private:
    BOOLEAN ProducerAlive();

    PMDL                   m_mdl;
    PVOID                  m_systemVa;      /* kernel VA, always valid */
    PVOID                  m_userVa;        /* user VA, only while mapped */
    PEPROCESS              m_clientProcess;
    LONG                   m_claimed;       /* interlocked 0/1 */
    KSPIN_LOCK             m_lock;          /* guards map/unmap, not the audio path */
    SMARTMIC_RING_HEADER*  m_header;
    UCHAR*                 m_payload;
};

#endif
