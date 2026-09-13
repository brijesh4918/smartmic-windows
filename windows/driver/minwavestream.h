/*++
    minwavestream.h -- the WaveRT capture stream.

    A physical capture device's DMA engine fills the WaveRT buffer on its own.
    We have no DMA engine, so a periodic timer plays that role: every
    notification period it copies one period of PCM out of the service's ring
    into the WaveRT buffer and signals the notification event, which is exactly
    what the audio engine expects to observe.
--*/
#ifndef SMARTMIC_MINWAVESTREAM_H
#define SMARTMIC_MINWAVESTREAM_H

#include "common.h"
#include "ring.h"

class CMiniportWaveRT;

class CMiniportWaveRTStream : public IMiniportWaveRTStreamNotification,
                              public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveRTStream);
    ~CMiniportWaveRTStream();

    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

    NTSTATUS Init(_In_ CMiniportWaveRT* Miniport,
                  _In_ PPORTWAVERTSTREAM PortStream,
                  _In_ ULONG Pin,
                  _In_ BOOLEAN Capture,
                  _In_ PKSDATAFORMAT DataFormat,
                  _In_ CSmartMicRing* Ring);

    /* Timer callback body, called at DISPATCH_LEVEL. */
    void OnTimerTick();

private:
    void StartTimer();
    void StopTimer();
    void FreeBufferInternal();

    CMiniportWaveRT*   m_miniport;
    PPORTWAVERTSTREAM  m_portStream;
    CSmartMicRing*     m_ring;

    ULONG              m_pin;
    BOOLEAN            m_capture;
    KSSTATE            m_state;

    /* WaveRT buffer, allocated on behalf of the audio engine. */
    PMDL               m_bufferMdl;
    PVOID              m_bufferVa;
    ULONG              m_bufferBytes;
    ULONG              m_notificationsPerBuffer;
    ULONG              m_bytesPerNotification;

    /* Where the "hardware" has written up to, in bytes, modulo the buffer. */
    ULONG              m_writeOffset;
    ULONGLONG          m_framesWritten;

    KTIMER             m_timer;
    KDPC               m_dpc;
    BOOLEAN            m_timerRunning;

    /* Notification events registered by the audio engine. One is the
       documented norm; the array keeps the code honest if more arrive. */
    static const ULONG kMaxNotificationEvents = 4;
    PKEVENT            m_notificationEvents[kMaxNotificationEvents];
    ULONG              m_notificationEventCount;

    KSPIN_LOCK         m_lock;
};

#endif
