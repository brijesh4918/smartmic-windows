/*++
    minwave.cpp -- descriptors and the miniport for the capture endpoint.

    The format is deliberately a single point, not a range: 48 kHz, mono,
    16-bit, for the entire life of the endpoint. Applications that cache the
    format at call start therefore never see it change (risk R2).
--*/
#include "minwave.h"
#include "minwavestream.h"

#pragma code_seg("PAGE")

/* ---------------------------------------------------------------- formats */

static KSDATAFORMAT_WAVEFORMATEX SmartMicPcmFormat =
{
    {
        sizeof(KSDATAFORMAT_WAVEFORMATEX),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    {
        WAVE_FORMAT_PCM,
        SMARTMIC_CHANNELS,
        SMARTMIC_SAMPLE_RATE,
        SMARTMIC_SAMPLE_RATE * SMARTMIC_BYTES_PER_FRAME,
        SMARTMIC_BYTES_PER_FRAME,
        SMARTMIC_BITS,
        0
    }
};

static KSDATARANGE_AUDIO SmartMicStreamDataRange =
{
    {
        sizeof(KSDATARANGE_AUDIO),
        0,
        0,
        0,
        STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
        STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
        STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)
    },
    SMARTMIC_CHANNELS,
    SMARTMIC_BITS,
    SMARTMIC_BITS,
    SMARTMIC_SAMPLE_RATE,
    SMARTMIC_SAMPLE_RATE
};

static PKSDATARANGE SmartMicStreamDataRanges[] =
{
    PKSDATARANGE(&SmartMicStreamDataRange)
};

static KSDATARANGE SmartMicBridgeDataRange =
{
    sizeof(KSDATARANGE),
    0,
    0,
    0,
    STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
    STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
};

static PKSDATARANGE SmartMicBridgeDataRanges[] =
{
    &SmartMicBridgeDataRange
};

/* ------------------------------------------------------------------- pins */

static PCPIN_DESCRIPTOR SmartMicWavePins[] =
{
    /* KSPIN_WAVE_CAPTURE_SINK -- the pin applications stream from. */
    {
        1, 1, 0, NULL,                                  /* MaxGlobal/Filter/Possible, Automation */
        {
            0, NULL,                                    /* interfaces: defaults */
            0, NULL,                                    /* mediums: defaults */
            SIZEOF_ARRAY(SmartMicStreamDataRanges),
            SmartMicStreamDataRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_SINK,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    },
    /* KSPIN_WAVE_CAPTURE_SOURCE -- bridge pin, wired to the topology filter. */
    {
        0, 0, 0, NULL,
        {
            0, NULL,
            0, NULL,
            SIZEOF_ARRAY(SmartMicBridgeDataRanges),
            SmartMicBridgeDataRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    }
};

static PCCONNECTION_DESCRIPTOR SmartMicWaveConnections[] =
{
    { PCFILTER_NODE, KSPIN_WAVE_CAPTURE_SOURCE, PCFILTER_NODE, KSPIN_WAVE_CAPTURE_SINK }
};

static GUID SmartMicWaveCategories[] =
{
    STATICGUIDOF(KSCATEGORY_AUDIO),
    STATICGUIDOF(KSCATEGORY_CAPTURE),
    STATICGUIDOF(KSCATEGORY_REALTIME)
};

PCFILTER_DESCRIPTOR SmartMicWaveFilterDescriptor =
{
    0,                                          /* Version */
    NULL,                                       /* AutomationTable */
    sizeof(PCPIN_DESCRIPTOR),
    SIZEOF_ARRAY(SmartMicWavePins),
    SmartMicWavePins,
    sizeof(PCNODE_DESCRIPTOR),
    0,
    NULL,
    SIZEOF_ARRAY(SmartMicWaveConnections),
    SmartMicWaveConnections,
    SIZEOF_ARRAY(SmartMicWaveCategories),
    SmartMicWaveCategories
};

/* -------------------------------------------------------------- miniport */

NTSTATUS CreateMiniportWaveRTSmartMic(_Out_ PUNKNOWN* Unknown,
                                      _In_ REFCLSID,
                                      _In_opt_ PUNKNOWN UnknownOuter,
                                      _In_ POOL_TYPE PoolType,
                                      _In_ PUNKNOWN UnknownAdapter,
                                      _In_opt_ PVOID DeviceContext)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(UnknownAdapter);

    CMiniportWaveRT* obj = new (PoolType, SMARTMIC_POOLTAG) CMiniportWaveRT(UnknownOuter);
    if (obj == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    obj->AddRef();
    obj->InitRing((CSmartMicRing*)DeviceContext);
    *Unknown = reinterpret_cast<PUNKNOWN>(obj);
    return STATUS_SUCCESS;
}

CMiniportWaveRT::~CMiniportWaveRT()
{
    PAGED_CODE();
    if (m_port != NULL) {
        m_port->Release();
        m_port = NULL;
    }
    SmTrace("wave miniport destroyed");
}

NTSTATUS CMiniportWaveRT::NonDelegatingQueryInterface(_In_ REFIID Interface,
                                                      _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(PMINIPORTWAVERT(this)));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniport)) {
        *Object = PVOID(PMINIPORT(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniportWaveRT)) {
        *Object = PVOID(PMINIPORTWAVERT(this));
    } else {
        *Object = NULL;
    }

    if (*Object != NULL) {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CMiniportWaveRT::Init(_In_opt_ PUNKNOWN UnknownAdapter,
                               _In_opt_ PRESOURCELIST ResourceList,
                               _In_ PPORTWAVERT Port)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);

    m_port = Port;
    m_port->AddRef();
    m_stream = NULL;

    SmTrace("wave miniport initialised");
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportWaveRT::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* Description)
{
    PAGED_CODE();
    *Description = &SmartMicWaveFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportWaveRT::DataRangeIntersection(_In_ ULONG PinId,
                                                _In_ PKSDATARANGE ClientDataRange,
                                                _In_ PKSDATARANGE MyDataRange,
                                                _In_ ULONG OutputBufferLength,
                                                _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultantFormatLength) PVOID ResultantFormat,
                                                _Out_ PULONG ResultantFormatLength)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PinId);
    UNREFERENCED_PARAMETER(ClientDataRange);
    UNREFERENCED_PARAMETER(MyDataRange);

    /* There is exactly one supported format, so the intersection is either
       that format or nothing. Doing it here rather than deferring to the port
       driver keeps the answer unambiguous. */
    if (OutputBufferLength == 0) {
        *ResultantFormatLength = sizeof(KSDATAFORMAT_WAVEFORMATEX);
        return STATUS_BUFFER_OVERFLOW;
    }
    if (OutputBufferLength < sizeof(KSDATAFORMAT_WAVEFORMATEX)) {
        *ResultantFormatLength = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(ResultantFormat, &SmartMicPcmFormat, sizeof(KSDATAFORMAT_WAVEFORMATEX));
    ((PKSDATAFORMAT)ResultantFormat)->FormatSize = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    *ResultantFormatLength = sizeof(KSDATAFORMAT_WAVEFORMATEX);
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportWaveRT::NewStream(_Out_ PMINIPORTWAVERTSTREAM* Stream,
                                    _In_ PPORTWAVERTSTREAM PortStream,
                                    _In_ ULONG Pin,
                                    _In_ BOOLEAN Capture,
                                    _In_ PKSDATAFORMAT DataFormat)
{
    PAGED_CODE();

    *Stream = NULL;

    if (Pin != KSPIN_WAVE_CAPTURE_SINK || !Capture) {
        SmTrace("NewStream refused: pin %u capture %d", Pin, (int)Capture);
        return STATUS_INVALID_PARAMETER;
    }
    if (m_stream != NULL) {
        return STATUS_DEVICE_BUSY;
    }

    CMiniportWaveRTStream* stream =
        new (NonPagedPoolNx, SMARTMIC_POOLTAG) CMiniportWaveRTStream(NULL);
    if (stream == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    stream->AddRef();

    const NTSTATUS status = stream->Init(this, PortStream, Pin, Capture, DataFormat, m_ring);
    if (!NT_SUCCESS(status)) {
        stream->Release();
        return status;
    }

    m_stream = stream;
    *Stream = PMINIPORTWAVERTSTREAM(stream);
    return STATUS_SUCCESS;
}

void CMiniportWaveRT::StreamClosed(_In_ ULONG Pin, _In_ CMiniportWaveRTStream* Stream)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Pin);
    if (m_stream == Stream) {
        m_stream = NULL;
    }
}

NTSTATUS CMiniportWaveRT::GetDeviceDescription(_Out_ PDEVICE_DESCRIPTION DeviceDescription)
{
    PAGED_CODE();
    RtlZeroMemory(DeviceDescription, sizeof(DEVICE_DESCRIPTION));
    DeviceDescription->Master = TRUE;
    DeviceDescription->ScatterGather = TRUE;
    DeviceDescription->Dma32BitAddresses = TRUE;
    DeviceDescription->InterfaceType = PCIBus;
    DeviceDescription->MaximumLength = 0xFFFFFFFF;
    return STATUS_SUCCESS;
}

#pragma code_seg()
