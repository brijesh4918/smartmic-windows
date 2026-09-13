/*++
    minwave.h -- the WaveRT capture miniport for "Smart Microphone".
--*/
#ifndef SMARTMIC_MINWAVE_H
#define SMARTMIC_MINWAVE_H

#include "common.h"
#include "ring.h"

#define KSPIN_WAVE_CAPTURE_SINK    0   /* streaming pin: what applications open */
#define KSPIN_WAVE_CAPTURE_SOURCE  1   /* bridge pin: wired to the topology filter */

class CMiniportWaveRTStream;

class CMiniportWaveRT : public IMiniportWaveRT,
                        public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportWaveRT);
    ~CMiniportWaveRT();

    IMP_IMiniportWaveRT;

    NTSTATUS InitRing(_In_ CSmartMicRing* Ring) { m_ring = Ring; return STATUS_SUCCESS; }
    void StreamClosed(_In_ ULONG Pin, _In_ CMiniportWaveRTStream* Stream);

private:
    PADAPTERCOMMON  m_adapterCommon;
    PPORTWAVERT     m_port;
    CSmartMicRing*  m_ring;
    /* One capture stream at a time. The audio engine is the only client of a
       WaveRT capture pin, so a second open is a bug somewhere, not a feature. */
    CMiniportWaveRTStream* m_stream;
};

#endif
