/*++
    minwavestream.cpp
--*/
#include "minwavestream.h"
#include "minwave.h"

#pragma code_seg("PAGE")

static KDEFERRED_ROUTINE SmartMicStreamDpc;

CMiniportWaveRTStream::~CMiniportWaveRTStream()
{
    PAGED_CODE();

    StopTimer();
    FreeBufferInternal();

    if (m_ring != NULL) {
        m_ring->SetStreamOpen(FALSE);
    }
    if (m_portStream != NULL) {
        m_portStream->Release();
        m_portStream = NULL;
    }
    if (m_miniport != NULL) {
        m_miniport->StreamClosed(m_pin, this);
        m_miniport->Release();
        m_miniport = NULL;
    }
    SmTrace("stream destroyed");
}

NTSTATUS CMiniportWaveRTStream::NonDelegatingQueryInterface(_In_ REFIID Interface,
                                                            _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERTSTREAM(this)));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStream)) {
        *Object = PVOID(PMINIPORTWAVERTSTREAM(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRTStreamNotification)) {
        *Object = PVOID(PMINIPORTWAVERTSTREAMNOTIFICATION(this));
    } else {
        *Object = NULL;
    }

    if (*Object != NULL) {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CMiniportWaveRTStream::Init(_In_ CMiniportWaveRT* Miniport,
                                     _In_ PPORTWAVERTSTREAM PortStream,
                                     _In_ ULONG Pin,
                                     _In_ BOOLEAN Capture,
                                     _In_ PKSDATAFORMAT DataFormat,
                                     _In_ CSmartMicRing* Ring)
{
    PAGED_CODE();

    m_miniport = Miniport;
    m_miniport->AddRef();
    m_portStream = PortStream;
    m_portStream->AddRef();
    m_ring = Ring;
    m_pin = Pin;
    m_capture = Capture;
    m_state = KSSTATE_STOP;

    m_bufferMdl = NULL;
    m_bufferVa = NULL;
    m_bufferBytes = 0;
    m_notificationsPerBuffer = 0;
    m_bytesPerNotification = 0;
    m_writeOffset = 0;
    m_framesWritten = 0;
    m_timerRunning = FALSE;
    m_notificationEventCount = 0;
    RtlZeroMemory(m_notificationEvents, sizeof(m_notificationEvents));

    KeInitializeSpinLock(&m_lock);
    KeInitializeTimerEx(&m_timer, SynchronizationTimer);
    KeInitializeDpc(&m_dpc, SmartMicStreamDpc, this);

    /* The endpoint format is fixed for the life of the device (ADR-003), so a
       mismatch here means something upstream renegotiated, which must fail
       loudly rather than produce garbage. */
    PWAVEFORMATEX wfx = (PWAVEFORMATEX)(DataFormat + 1);
    if (DataFormat->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEX) ||
        wfx->nSamplesPerSec != SMARTMIC_SAMPLE_RATE ||
        wfx->nChannels != SMARTMIC_CHANNELS ||
        wfx->wBitsPerSample != SMARTMIC_BITS) {
        SmTrace("rejected stream format %u Hz %u ch %u bit",
                wfx->nSamplesPerSec, wfx->nChannels, wfx->wBitsPerSample);
        return STATUS_INVALID_PARAMETER;
    }

    m_ring->SetStreamOpen(TRUE);
    SmTrace("stream opened on pin %u", Pin);
    return STATUS_SUCCESS;
}

void CMiniportWaveRTStream::FreeBufferInternal()
{
    PAGED_CODE();

    if (m_bufferMdl != NULL && m_portStream != NULL) {
        m_portStream->UnmapAllocatedPages(m_bufferVa, m_bufferMdl);
        m_portStream->FreePagesFromMdl(m_bufferMdl);
        m_bufferMdl = NULL;
        m_bufferVa = NULL;
        m_bufferBytes = 0;
    }
}

NTSTATUS CMiniportWaveRTStream::AllocateAudioBuffer(_In_ ULONG RequestedSize,
                                                    _Out_ PMDL* AudioBufferMdl,
                                                    _Out_ ULONG* ActualSize,
                                                    _Out_ ULONG* OffsetFromFirstPage,
                                                    _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    PAGED_CODE();
    /* Without notifications there is nothing to drive the copy, so route
       through the notification path with a single notification. */
    return AllocateBufferWithNotification(1, RequestedSize, AudioBufferMdl, ActualSize,
                                          OffsetFromFirstPage, CacheType);
}

void CMiniportWaveRTStream::FreeAudioBuffer(_In_opt_ PMDL Mdl, _In_ ULONG Size)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(Size);
    FreeBufferInternal();
}

NTSTATUS CMiniportWaveRTStream::AllocateBufferWithNotification(
    _In_ ULONG NotificationCount,
    _In_ ULONG RequestedSize,
    _Out_ PMDL* AudioBufferMdl,
    _Out_ ULONG* ActualSize,
    _Out_ ULONG* OffsetFromFirstPage,
    _Out_ MEMORY_CACHING_TYPE* CacheType)
{
    PAGED_CODE();

    if (RequestedSize == 0 || NotificationCount == 0) {
        return STATUS_UNSUCCESSFUL;
    }

    /* Round down to a whole number of frames, and to a multiple of the
       notification count, so every notification is the same size. A ragged
       last period is the classic source of clicks in virtual audio drivers. */
    ULONG size = RequestedSize - (RequestedSize % (SMARTMIC_BYTES_PER_FRAME * NotificationCount));
    if (size == 0) {
        return STATUS_UNSUCCESSFUL;
    }

    PHYSICAL_ADDRESS high;
    high.QuadPart = MAXULONG64;

    PMDL mdl = m_portStream->AllocatePagesForMdl(high, size);
    if (mdl == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PVOID va = m_portStream->MapAllocatedPages(mdl, MmCached);
    if (va == NULL) {
        m_portStream->FreePagesFromMdl(mdl);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(va, size);

    m_bufferMdl = mdl;
    m_bufferVa = va;
    m_bufferBytes = size;
    m_notificationsPerBuffer = NotificationCount;
    m_bytesPerNotification = size / NotificationCount;
    m_writeOffset = 0;
    m_framesWritten = 0;

    *AudioBufferMdl = mdl;
    *ActualSize = size;
    *OffsetFromFirstPage = 0;
    *CacheType = MmCached;

    SmTrace("wavert buffer %u bytes, %u notifications of %u bytes",
            size, NotificationCount, m_bytesPerNotification);
    return STATUS_SUCCESS;
}

void CMiniportWaveRTStream::FreeBufferWithNotification(_In_opt_ PMDL Mdl, _In_ ULONG Size)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(Size);
    StopTimer();
    FreeBufferInternal();
}

NTSTATUS CMiniportWaveRTStream::RegisterNotificationEvent(_In_ PKEVENT NotificationEvent)
{
    PAGED_CODE();

    KIRQL irql;
    KeAcquireSpinLock(&m_lock, &irql);

    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    for (ULONG i = 0; i < kMaxNotificationEvents; ++i) {
        if (m_notificationEvents[i] == NotificationEvent) {
            status = STATUS_UNSUCCESSFUL;   /* already registered */
            break;
        }
    }
    if (status != STATUS_UNSUCCESSFUL) {
        for (ULONG i = 0; i < kMaxNotificationEvents; ++i) {
            if (m_notificationEvents[i] == NULL) {
                m_notificationEvents[i] = NotificationEvent;
                ++m_notificationEventCount;
                status = STATUS_SUCCESS;
                break;
            }
        }
    }

    KeReleaseSpinLock(&m_lock, irql);
    return status;
}

NTSTATUS CMiniportWaveRTStream::UnregisterNotificationEvent(_In_ PKEVENT NotificationEvent)
{
    PAGED_CODE();

    KIRQL irql;
    KeAcquireSpinLock(&m_lock, &irql);

    NTSTATUS status = STATUS_NOT_FOUND;
    for (ULONG i = 0; i < kMaxNotificationEvents; ++i) {
        if (m_notificationEvents[i] == NotificationEvent) {
            m_notificationEvents[i] = NULL;
            --m_notificationEventCount;
            status = STATUS_SUCCESS;
            break;
        }
    }

    KeReleaseSpinLock(&m_lock, irql);
    return status;
}

NTSTATUS CMiniportWaveRTStream::GetClockRegister(_Out_ PKSRTAUDIO_HWREGISTER Register)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Register);
    /* No hardware clock register to expose. Returning NOT_IMPLEMENTED makes
       the audio engine use GetPosition instead, which is what we support. */
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS CMiniportWaveRTStream::GetPositionRegister(_Out_ PKSRTAUDIO_HWREGISTER Register)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_IMPLEMENTED;
}

void CMiniportWaveRTStream::GetHWLatency(_Out_ PKSRTAUDIO_HWLATENCY Latency)
{
    PAGED_CODE();
    Latency->ChipsetDelay = 0;
    Latency->CodecDelay = 0;
    /* One notification period of inherent delay: that is genuinely how far
       behind the "hardware" write pointer is. Reporting zero here would make
       the audio engine schedule too aggressively and glitch. */
    Latency->FifoSize = m_bytesPerNotification;
}

NTSTATUS CMiniportWaveRTStream::SetFormat(_In_ PKSDATAFORMAT DataFormat)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(DataFormat);
    /* The endpoint format never changes (risk R2). */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS CMiniportWaveRTStream::SetState(_In_ KSSTATE State)
{
    PAGED_CODE();

    switch (State) {
    case KSSTATE_STOP:
        StopTimer();
        m_writeOffset = 0;
        m_framesWritten = 0;
        if (m_bufferVa != NULL) {
            RtlZeroMemory(m_bufferVa, m_bufferBytes);
        }
        break;

    case KSSTATE_ACQUIRE:
    case KSSTATE_PAUSE:
        StopTimer();
        break;

    case KSSTATE_RUN:
        if (m_bufferVa == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }
        if (m_ring != NULL) {
            m_ring->SetStreamOpen(TRUE);   /* resets readIndex to "now" */
        }
        StartTimer();
        break;
    }

    m_state = State;
    SmTrace("stream state -> %d", (int)State);
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportWaveRTStream::GetPosition(_Out_ PKSAUDIO_POSITION Position)
{
    KIRQL irql;
    KeAcquireSpinLock(&m_lock, &irql);

    /* For capture, PlayOffset is how much the client may safely read and
       WriteOffset is how far the hardware has filled. Keeping them equal is
       correct for a device with no read-ahead. */
    Position->PlayOffset = m_framesWritten * SMARTMIC_BYTES_PER_FRAME;
    Position->WriteOffset = Position->PlayOffset;

    KeReleaseSpinLock(&m_lock, irql);
    return STATUS_SUCCESS;
}

#pragma code_seg()

void CMiniportWaveRTStream::StartTimer()
{
    if (m_timerRunning) {
        return;
    }
    m_timerRunning = TRUE;

    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)SMARTMIC_NOTIFY_MS * 10000LL;   /* relative, 100 ns */
    KeSetTimerEx(&m_timer, due, SMARTMIC_NOTIFY_MS, &m_dpc);
}

void CMiniportWaveRTStream::StopTimer()
{
    if (!m_timerRunning) {
        return;
    }
    m_timerRunning = FALSE;
    KeCancelTimer(&m_timer);
    KeFlushQueuedDpcs();
}

void CMiniportWaveRTStream::OnTimerTick()
{
    KIRQL irql;
    KeAcquireSpinLock(&m_lock, &irql);

    if (!m_timerRunning || m_bufferVa == NULL || m_bytesPerNotification == 0) {
        KeReleaseSpinLock(&m_lock, irql);
        return;
    }

    const ULONG bytes = m_bytesPerNotification;
    const ULONG frames = bytes / SMARTMIC_BYTES_PER_FRAME;
    UCHAR* dst = ((UCHAR*)m_bufferVa) + m_writeOffset;

    /* This is the whole job: one period out of the ring, into the buffer the
       audio engine is reading. A starve is silence, never a stall. */
    if (m_ring != NULL) {
        m_ring->Read(dst, frames);
    } else {
        RtlZeroMemory(dst, bytes);
    }

    m_writeOffset += bytes;
    if (m_writeOffset >= m_bufferBytes) {
        m_writeOffset = 0;
    }
    m_framesWritten += frames;

    PKEVENT events[kMaxNotificationEvents];
    RtlCopyMemory(events, m_notificationEvents, sizeof(events));

    KeReleaseSpinLock(&m_lock, irql);

    /* Signal outside the lock: KeSetEvent can reschedule, and holding a spin
       lock across it is how a virtual audio driver earns a deadlock. */
    for (ULONG i = 0; i < kMaxNotificationEvents; ++i) {
        if (events[i] != NULL) {
            KeSetEvent(events[i], 0, FALSE);
        }
    }
}

static void SmartMicStreamDpc(_In_ PKDPC Dpc, _In_opt_ PVOID Context,
                              _In_opt_ PVOID Arg1, _In_opt_ PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);

    if (Context != NULL) {
        ((CMiniportWaveRTStream*)Context)->OnTimerTick();
    }
}
