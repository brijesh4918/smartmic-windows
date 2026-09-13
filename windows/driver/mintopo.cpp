/*++
    mintopo.cpp
--*/
#include "mintopo.h"

#pragma code_seg("PAGE")

static KSDATARANGE SmartMicTopoBridgeRange =
{
    sizeof(KSDATARANGE),
    0, 0, 0,
    STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),
    STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
};

static PKSDATARANGE SmartMicTopoRanges[] = { &SmartMicTopoBridgeRange };

/* A jack description makes the endpoint present as a connected internal
   microphone instead of an unplugged jack, which is what stops Windows from
   showing it greyed out in Sound settings. */
static KSJACK_DESCRIPTION SmartMicJackDescription =
{
    KSAUDIO_SPEAKER_MONO,
    0x0000,                       /* colour: unknown */
    eConnTypeUnknown,
    eGeoLocNotApplicable,
    eGenLocInternal,
    ePortConnIntegratedDevice,
    TRUE                          /* jack is "connected" */
};

static PKSJACK_DESCRIPTION SmartMicJackDescriptions[] =
{
    &SmartMicJackDescription,
    NULL
};

static NTSTATUS SmartMicPropertyJackDescription(_In_ PPCPROPERTY_REQUEST Request)
{
    PAGED_CODE();

    if (Request->Node != ULONG(-1)) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (!(Request->Verb & KSPROPERTY_TYPE_GET)) {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (Request->InstanceSize < sizeof(ULONG)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    const ULONG pinId = *((PULONG)Request->Instance);
    if (pinId != KSPIN_TOPO_MIC_ELEMENT) {
        return STATUS_INVALID_PARAMETER;
    }

    const ULONG needed = sizeof(KSMULTIPLE_ITEM) + sizeof(KSJACK_DESCRIPTION);
    if (Request->ValueSize == 0) {
        Request->ValueSize = needed;
        return STATUS_BUFFER_OVERFLOW;
    }
    if (Request->ValueSize < needed) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    PKSMULTIPLE_ITEM item = (PKSMULTIPLE_ITEM)Request->Value;
    item->Size = needed;
    item->Count = 1;
    RtlCopyMemory(item + 1, &SmartMicJackDescription, sizeof(KSJACK_DESCRIPTION));
    Request->ValueSize = needed;
    return STATUS_SUCCESS;
}

static PCPROPERTY_ITEM SmartMicTopoFilterProperties[] =
{
    {
        &KSPROPSETID_Jack,
        KSPROPERTY_JACK_DESCRIPTION,
        KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_BASICSUPPORT,
        SmartMicPropertyJackDescription
    }
};

DEFINE_PCAUTOMATION_TABLE_PROP(SmartMicTopoFilterAutomation, SmartMicTopoFilterProperties);

static PCPIN_DESCRIPTOR SmartMicTopoPins[] =
{
    /* KSPIN_TOPO_MIC_ELEMENT */
    {
        0, 0, 0, NULL,
        {
            0, NULL,
            0, NULL,
            SIZEOF_ARRAY(SmartMicTopoRanges),
            SmartMicTopoRanges,
            KSPIN_DATAFLOW_IN,
            KSPIN_COMMUNICATION_NONE,
            &KSNODETYPE_MICROPHONE,
            &KSAUDFNAME_MICROPHONE,
            0
        }
    },
    /* KSPIN_TOPO_BRIDGE */
    {
        0, 0, 0, NULL,
        {
            0, NULL,
            0, NULL,
            SIZEOF_ARRAY(SmartMicTopoRanges),
            SmartMicTopoRanges,
            KSPIN_DATAFLOW_OUT,
            KSPIN_COMMUNICATION_NONE,
            &KSCATEGORY_AUDIO,
            NULL,
            0
        }
    }
};

static PCCONNECTION_DESCRIPTOR SmartMicTopoConnections[] =
{
    { PCFILTER_NODE, KSPIN_TOPO_MIC_ELEMENT, PCFILTER_NODE, KSPIN_TOPO_BRIDGE }
};

static GUID SmartMicTopoCategories[] =
{
    STATICGUIDOF(KSCATEGORY_AUDIO),
    STATICGUIDOF(KSCATEGORY_TOPOLOGY)
};

PCFILTER_DESCRIPTOR SmartMicTopoFilterDescriptor =
{
    0,
    &SmartMicTopoFilterAutomation,
    sizeof(SmartMicTopoPins[0]),
    SIZEOF_ARRAY(SmartMicTopoPins),
    SmartMicTopoPins,
    sizeof(PCNODE_DESCRIPTOR),
    0,
    NULL,
    SIZEOF_ARRAY(SmartMicTopoConnections),
    SmartMicTopoConnections,
    SIZEOF_ARRAY(SmartMicTopoCategories),
    SmartMicTopoCategories
};

NTSTATUS CreateMiniportTopologySmartMic(_Out_ PUNKNOWN* Unknown,
                                        _In_ REFCLSID,
                                        _In_opt_ PUNKNOWN UnknownOuter,
                                        _In_ POOL_TYPE PoolType,
                                        _In_ PUNKNOWN UnknownAdapter,
                                        _In_opt_ PVOID DeviceContext)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(DeviceContext);

    CMiniportTopology* obj = new (PoolType, SMARTMIC_POOLTAG) CMiniportTopology(UnknownOuter);
    if (obj == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    obj->AddRef();
    *Unknown = reinterpret_cast<PUNKNOWN>(obj);
    return STATUS_SUCCESS;
}

CMiniportTopology::~CMiniportTopology()
{
    PAGED_CODE();
    if (m_port != NULL) {
        m_port->Release();
        m_port = NULL;
    }
}

NTSTATUS CMiniportTopology::NonDelegatingQueryInterface(_In_ REFIID Interface,
                                                        _COM_Outptr_ PVOID* Object)
{
    PAGED_CODE();

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(PMINIPORTTOPOLOGY(this)));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniport)) {
        *Object = PVOID(PMINIPORT(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IMiniportTopology)) {
        *Object = PVOID(PMINIPORTTOPOLOGY(this));
    } else {
        *Object = NULL;
    }

    if (*Object != NULL) {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS CMiniportTopology::Init(_In_opt_ PUNKNOWN UnknownAdapter,
                                 _In_opt_ PRESOURCELIST ResourceList,
                                 _In_ PPORTTOPOLOGY Port)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(UnknownAdapter);
    UNREFERENCED_PARAMETER(ResourceList);
    m_port = Port;
    m_port->AddRef();
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportTopology::GetDescription(_Out_ PPCFILTER_DESCRIPTOR* Description)
{
    PAGED_CODE();
    *Description = &SmartMicTopoFilterDescriptor;
    return STATUS_SUCCESS;
}

NTSTATUS CMiniportTopology::DataRangeIntersection(_In_ ULONG PinId,
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
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ResultantFormat);
    UNREFERENCED_PARAMETER(ResultantFormatLength);
    /* Bridge pins carry no data format to intersect. */
    return STATUS_NOT_IMPLEMENTED;
}

#pragma code_seg()
