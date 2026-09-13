/*++
    ring.cpp -- kernel side of the service->driver PCM ring.
--*/
#include "ring.h"

#pragma code_seg("PAGE")

CSmartMicRing::CSmartMicRing()
    : m_mdl(NULL), m_systemVa(NULL), m_userVa(NULL),
      m_clientProcess(NULL), m_claimed(0), m_header(NULL), m_payload(NULL)
{
    PAGED_CODE();
    KeInitializeSpinLock(&m_lock);
}

CSmartMicRing::~CSmartMicRing()
{
    PAGED_CODE();
    Cleanup();
}

NTSTATUS CSmartMicRing::Initialize()
{
    PAGED_CODE();

    /* Non-paged: the drain runs at DISPATCH_LEVEL from the WaveRT timer. */
    m_systemVa = ExAllocatePool2(POOL_FLAG_NON_PAGED, SMARTMIC_RING_TOTAL_BYTES, SMARTMIC_POOLTAG);
    if (m_systemVa == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(m_systemVa, SMARTMIC_RING_TOTAL_BYTES);

    m_mdl = IoAllocateMdl(m_systemVa, SMARTMIC_RING_TOTAL_BYTES, FALSE, FALSE, NULL);
    if (m_mdl == NULL) {
        ExFreePoolWithTag(m_systemVa, SMARTMIC_POOLTAG);
        m_systemVa = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmBuildMdlForNonPagedPool(m_mdl);

    m_header  = (SMARTMIC_RING_HEADER*)m_systemVa;
    m_payload = ((UCHAR*)m_systemVa) + SMARTMIC_RING_HEADER_BYTES;

    m_header->magic          = SMARTMIC_RING_MAGIC;
    m_header->version        = SMARTMIC_RING_VERSION;
    m_header->capacityFrames = SMARTMIC_RING_FRAMES;
    m_header->channels       = SMARTMIC_CHANNELS;
    m_header->sampleRate     = SMARTMIC_SAMPLE_RATE;
    m_header->bytesPerFrame  = SMARTMIC_BYTES_PER_FRAME;
    m_header->payloadOffset  = SMARTMIC_RING_HEADER_BYTES;

    SmTrace("ring initialised, %u bytes", (ULONG)SMARTMIC_RING_TOTAL_BYTES);
    return STATUS_SUCCESS;
}

void CSmartMicRing::Cleanup()
{
    PAGED_CODE();

    if (m_mdl != NULL) {
        if (m_userVa != NULL) {
            /* Only valid in the client's address space; if that process is
               already gone, the mapping went with it. */
            __try {
                MmUnmapLockedPages(m_userVa, m_mdl);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                SmTrace("unmap during cleanup raised; process likely gone");
            }
            m_userVa = NULL;
        }
        IoFreeMdl(m_mdl);
        m_mdl = NULL;
    }
    if (m_systemVa != NULL) {
        ExFreePoolWithTag(m_systemVa, SMARTMIC_POOLTAG);
        m_systemVa = NULL;
    }
    m_header = NULL;
    m_payload = NULL;
    m_clientProcess = NULL;
    InterlockedExchange(&m_claimed, 0);
}

NTSTATUS CSmartMicRing::MapForCurrentProcess(_Out_ PVOID* UserAddress, _Out_ ULONG* TotalBytes)
{
    PAGED_CODE();

    *UserAddress = NULL;
    *TotalBytes = 0;

    if (m_mdl == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Single-client claim. Taken before any mapping work so two racing
       services cannot both believe they own the ring. */
    if (InterlockedCompareExchange(&m_claimed, 1, 0) != 0) {
        SmTrace("ring already claimed by another client");
        return STATUS_DEVICE_BUSY;
    }

    PVOID userVa = NULL;
    __try {
        userVa = MmMapLockedPagesSpecifyCache(m_mdl,
                                              UserMode,
                                              MmCached,
                                              NULL,
                                              FALSE,
                                              NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        userVa = NULL;
    }

    if (userVa == NULL) {
        InterlockedExchange(&m_claimed, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    m_userVa = userVa;
    m_clientProcess = PsGetCurrentProcess();

    *UserAddress = userVa;
    *TotalBytes = SMARTMIC_RING_TOTAL_BYTES;

    SmTrace("ring mapped for pid %p", PsGetCurrentProcessId());
    return STATUS_SUCCESS;
}

NTSTATUS CSmartMicRing::UnmapForCurrentProcess()
{
    PAGED_CODE();

    if (m_userVa == NULL || m_mdl == NULL) {
        return STATUS_SUCCESS;   /* idempotent by contract */
    }
    if (m_clientProcess != PsGetCurrentProcess()) {
        return STATUS_ACCESS_DENIED;
    }

    __try {
        MmUnmapLockedPages(m_userVa, m_mdl);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SmTrace("unmap raised");
    }
    m_userVa = NULL;
    m_clientProcess = NULL;
    InterlockedExchange(&m_claimed, 0);
    return STATUS_SUCCESS;
}

void CSmartMicRing::ReleaseClaimIfOwner(_In_ PEPROCESS Process)
{
    PAGED_CODE();

    if (m_clientProcess == Process) {
        /* The process is closing its handle. Its user mapping is torn down by
           the memory manager with the address space; we only drop the claim so
           a restarted service can map again. */
        m_userVa = NULL;
        m_clientProcess = NULL;
        InterlockedExchange(&m_claimed, 0);
        SmTrace("client handle closed; claim released");
    }
}

#pragma code_seg()

BOOLEAN CSmartMicRing::ProducerAlive()
{
    if (m_header == NULL) {
        return FALSE;
    }
    const ULONG64 beat = m_header->producerHeartbeat;
    if (beat == 0) {
        return FALSE;
    }
    const ULONG64 now = KeQueryInterruptTime();
    /* Unsigned arithmetic: a heartbeat from the future (a service with a
       broken clock) reads as a huge delta and is treated as dead, which is the
       safe direction. */
    return (now >= beat) && ((now - beat) < (ULONG64)SMARTMIC_PRODUCER_TIMEOUT_100NS);
}

ULONG CSmartMicRing::Read(_Out_writes_bytes_(Frames * SMARTMIC_BYTES_PER_FRAME) PVOID Destination,
                          _In_ ULONG Frames)
{
    UCHAR* dst = (UCHAR*)Destination;
    const ULONG wantBytes = Frames * SMARTMIC_BYTES_PER_FRAME;

    if (m_header == NULL || m_payload == NULL || Frames == 0) {
        if (dst != NULL) {
            RtlZeroMemory(dst, wantBytes);
        }
        return 0;
    }

    /* A service that has stopped writing is indistinguishable from a quiet
       room, by design: applications see silence rather than noise or a hang
       (ADR-008). */
    if (!ProducerAlive()) {
        RtlZeroMemory(dst, wantBytes);
        InterlockedAdd((volatile LONG*)&m_header->underrunCount, (LONG)Frames);
        return 0;
    }

    /* Acquire the producer's index before touching the payload it published. */
    const ULONG64 writeIndex = (ULONG64)InterlockedCompareExchange64(
        (volatile LONG64*)&m_header->writeIndex, 0, 0);
    ULONG64 readIndex = m_header->readIndex;

    /* Untrusted: user mode owns writeIndex. If it is behind our own read
       index, or implausibly far ahead, treat the ring as empty rather than
       computing a bogus length. */
    ULONG64 available = 0;
    if (writeIndex >= readIndex) {
        available = writeIndex - readIndex;
        if (available > SMARTMIC_RING_FRAMES) {
            /* The producer lapped us. Skip forward to the newest data: late
               audio is worse than lost audio. */
            const ULONG64 skipped = available - SMARTMIC_RING_FRAMES;
            readIndex += skipped;
            available = SMARTMIC_RING_FRAMES;
            InterlockedAdd((volatile LONG*)&m_header->overrunCount,
                           (LONG)min(skipped, (ULONG64)MAXLONG));
        }
    }

    const ULONG realFrames = (ULONG)min((ULONG64)Frames, available);

    if (realFrames > 0) {
        const ULONG offsetFrames = (ULONG)(readIndex & SMARTMIC_RING_MASK);
        const ULONG firstFrames  = min(realFrames, SMARTMIC_RING_FRAMES - offsetFrames);

        RtlCopyMemory(dst,
                      m_payload + (SIZE_T)offsetFrames * SMARTMIC_BYTES_PER_FRAME,
                      (SIZE_T)firstFrames * SMARTMIC_BYTES_PER_FRAME);

        if (realFrames > firstFrames) {
            RtlCopyMemory(dst + (SIZE_T)firstFrames * SMARTMIC_BYTES_PER_FRAME,
                          m_payload,
                          (SIZE_T)(realFrames - firstFrames) * SMARTMIC_BYTES_PER_FRAME);
        }
    }

    if (realFrames < Frames) {
        const ULONG shortfall = Frames - realFrames;
        RtlZeroMemory(dst + (SIZE_T)realFrames * SMARTMIC_BYTES_PER_FRAME,
                      (SIZE_T)shortfall * SMARTMIC_BYTES_PER_FRAME);
        InterlockedAdd((volatile LONG*)&m_header->underrunCount, (LONG)shortfall);
    }

    /* Advance past the whole request, including the silent shortfall. Holding
       readIndex back on a starve would make the stream drift permanently late. */
    m_header->readIndex = readIndex + Frames;
    m_header->framesDelivered += Frames;

    return realFrames;
}

void CSmartMicRing::SetStreamOpen(_In_ BOOLEAN Open)
{
    if (m_header != NULL) {
        m_header->streamOpen = Open ? 1u : 0u;
        if (Open) {
            /* A fresh stream starts from whatever the producer has published
               most recently, not from stale audio buffered before the open. */
            m_header->readIndex = m_header->writeIndex;
        }
    }
}

void CSmartMicRing::GetStats(_Out_ SMARTMIC_STATS* Stats)
{
    RtlZeroMemory(Stats, sizeof(*Stats));
    if (m_header != NULL) {
        Stats->framesDelivered = m_header->framesDelivered;
        Stats->underrunCount   = m_header->underrunCount;
        Stats->overrunCount    = m_header->overrunCount;
        Stats->streamOpen      = m_header->streamOpen;
    }
}
