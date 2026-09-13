/*++
    adapter.cpp -- registers the two subdevices and wires them together.

        topology filter                         wave filter
      [mic element] --> [bridge] ===PHYSICAL=== [bridge] --> [streaming pin]
                                  CONNECTION                        |
                                                                    v
                                                          Teams / Zoom / Chrome
--*/
#include "common.h"
#include "ring.h"
#include "minwave.h"
#include "mintopo.h"

#pragma code_seg("PAGE")

#define SMARTMIC_WAVE_NAME  L"Wave"
#define SMARTMIC_TOPO_NAME  L"Topology"

static NTSTATUS InstallSubdevice(_In_ PDEVICE_OBJECT DeviceObject,
                                 _In_ PIRP Irp,
                                 _In_ PWSTR Name,
                                 _In_ REFGUID PortClassId,
                                 _In_ PFNCREATEMINIPORT CreateMiniport,
                                 _In_opt_ PVOID DeviceContext,
                                 _In_ PRESOURCELIST ResourceList,
                                 _Out_opt_ PUNKNOWN* OutPortUnknown)
{
    PAGED_CODE();

    PPORT port = NULL;
    NTSTATUS status = PcNewPort(&port, PortClassId);
    if (!NT_SUCCESS(status)) {
        SmTrace("PcNewPort(%ws) failed 0x%08X", Name, status);
        return status;
    }

    PUNKNOWN miniport = NULL;
    status = CreateMiniport(&miniport,
                            PortClassId,
                            NULL,
                            NonPagedPoolNx,
                            (PUNKNOWN)port,
                            DeviceContext);
    if (!NT_SUCCESS(status)) {
        port->Release();
        SmTrace("CreateMiniport(%ws) failed 0x%08X", Name, status);
        return status;
    }

    status = port->Init(DeviceObject, Irp, miniport, NULL, ResourceList);
    if (NT_SUCCESS(status)) {
        status = PcRegisterSubdevice(DeviceObject, Name, port);
        if (!NT_SUCCESS(status)) {
            SmTrace("PcRegisterSubdevice(%ws) failed 0x%08X", Name, status);
        }
    } else {
        SmTrace("port->Init(%ws) failed 0x%08X", Name, status);
    }

    /* The miniport is owned by the port once Init succeeds; our reference goes
       away either way. */
    miniport->Release();

    if (NT_SUCCESS(status) && OutPortUnknown != NULL) {
        *OutPortUnknown = (PUNKNOWN)port;   /* transfers our reference */
    } else {
        port->Release();
    }
    return status;
}

/* Thin adapters so both creators match PFNCREATEMINIPORT. */
static NTSTATUS CreateWaveThunk(PUNKNOWN* Unknown, REFCLSID ClassId, PUNKNOWN Outer,
                                POOL_TYPE PoolType, PUNKNOWN Adapter, PVOID Context)
{
    PAGED_CODE();
    return CreateMiniportWaveRTSmartMic(Unknown, ClassId, Outer, PoolType, Adapter, Context);
}

static NTSTATUS CreateTopoThunk(PUNKNOWN* Unknown, REFCLSID ClassId, PUNKNOWN Outer,
                                POOL_TYPE PoolType, PUNKNOWN Adapter, PVOID Context)
{
    PAGED_CODE();
    return CreateMiniportTopologySmartMic(Unknown, ClassId, Outer, PoolType, Adapter, Context);
}

NTSTATUS SmartMicInstallSubdevices(_In_ PDEVICE_OBJECT DeviceObject,
                                   _In_ PIRP Irp,
                                   _In_ PRESOURCELIST ResourceList)
{
    PAGED_CODE();

    if (g_SmartMicContext == NULL || g_SmartMicContext->ring == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    g_SmartMicContext->deviceObject = DeviceObject;

    PUNKNOWN topoPort = NULL;
    PUNKNOWN wavePort = NULL;

    NTSTATUS status = InstallSubdevice(DeviceObject, Irp, (PWSTR)SMARTMIC_TOPO_NAME,
                                       CLSID_PortTopology, CreateTopoThunk,
                                       NULL, ResourceList, &topoPort);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = InstallSubdevice(DeviceObject, Irp, (PWSTR)SMARTMIC_WAVE_NAME,
                              CLSID_PortWaveRT, CreateWaveThunk,
                              g_SmartMicContext->ring, ResourceList, &wavePort);
    if (!NT_SUCCESS(status)) {
        topoPort->Release();
        return status;
    }

    /* Without this, Windows sees two unrelated filters and no microphone. */
    status = PcRegisterPhysicalConnection(DeviceObject,
                                          topoPort, KSPIN_TOPO_BRIDGE,
                                          wavePort, KSPIN_WAVE_CAPTURE_SOURCE);
    if (!NT_SUCCESS(status)) {
        SmTrace("PcRegisterPhysicalConnection failed 0x%08X", status);
    }

    /* PortCls keeps its own references to the registered subdevices. */
    wavePort->Release();
    topoPort->Release();

    SmTrace("subdevices installed: %s", NT_SUCCESS(status) ? "ok" : "connection failed");
    return status;
}

#pragma code_seg()
