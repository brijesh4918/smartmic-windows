/*++
    driver.cpp -- DriverEntry, AddDevice, and the private control interface.

    PortCls owns the audio IRPs. We chain IRP_MJ_DEVICE_CONTROL so the service
    can map the ring and read statistics, and forward everything we do not
    recognise straight back to PortCls.
--*/
#include "common.h"
#include "ring.h"

/* Placement new for kernel mode (standard <new> is unavailable). */
inline void* __cdecl operator new(size_t, void* p) { return p; }

PSMARTMIC_DEVICE_CONTEXT g_SmartMicContext = NULL;

static PDRIVER_DISPATCH g_PcDeviceControl = NULL;
static PDRIVER_DISPATCH g_PcClose = NULL;

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" DRIVER_ADD_DEVICE SmartMicAddDevice;

#pragma code_seg("PAGE")

/*++
    Every one of our IOCTLs validates its lengths with == rather than >=, and
    none of them sizes an allocation from user input. That is the entire
    mitigation for threat model T8, so it is deliberately boring.
--*/
static NTSTATUS SmartMicHandlePrivateIoctl(_In_ PIRP Irp,
                                           _In_ PIO_STACK_LOCATION Stack,
                                           _Out_ BOOLEAN* Handled)
{
    PAGED_CODE();

    *Handled = TRUE;
    const ULONG code = Stack->Parameters.DeviceIoControl.IoControlCode;
    const ULONG inLen = Stack->Parameters.DeviceIoControl.InputBufferLength;
    const ULONG outLen = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;

    PSMARTMIC_DEVICE_CONTEXT ctx = g_SmartMicContext;
    if (ctx == NULL || ctx->ring == NULL) {
        Irp->IoStatus.Information = 0;
        return STATUS_DEVICE_NOT_READY;
    }

    switch (code) {

    case IOCTL_SMARTMIC_GET_VERSION:
    {
        if (inLen != 0 || outLen != sizeof(SMARTMIC_VERSION_INFO) || buffer == NULL) {
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_PARAMETER;
        }
        SMARTMIC_VERSION_INFO info;
        info.driverVersion  = SMARTMIC_DRIVER_VERSION;
        info.ringVersion    = SMARTMIC_RING_VERSION;
        info.ringTotalBytes = SMARTMIC_RING_TOTAL_BYTES;
        info.reserved       = 0;
        RtlCopyMemory(buffer, &info, sizeof(info));
        Irp->IoStatus.Information = sizeof(info);
        return STATUS_SUCCESS;
    }

    case IOCTL_SMARTMIC_MAP_RING:
    {
        if (inLen != 0 || outLen != sizeof(SMARTMIC_MAP_RESULT) || buffer == NULL) {
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_PARAMETER;
        }
        SMARTMIC_MAP_RESULT result;
        RtlZeroMemory(&result, sizeof(result));

        const NTSTATUS status = ctx->ring->MapForCurrentProcess(&result.baseAddress,
                                                                &result.totalBytes);
        if (!NT_SUCCESS(status)) {
            Irp->IoStatus.Information = 0;
            return status;
        }
        RtlCopyMemory(buffer, &result, sizeof(result));
        Irp->IoStatus.Information = sizeof(result);
        return STATUS_SUCCESS;
    }

    case IOCTL_SMARTMIC_UNMAP_RING:
    {
        if (inLen != 0 || outLen != 0) {
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_PARAMETER;
        }
        Irp->IoStatus.Information = 0;
        return ctx->ring->UnmapForCurrentProcess();
    }

    case IOCTL_SMARTMIC_GET_STATS:
    {
        if (inLen != 0 || outLen != sizeof(SMARTMIC_STATS) || buffer == NULL) {
            Irp->IoStatus.Information = 0;
            return STATUS_INVALID_PARAMETER;
        }
        SMARTMIC_STATS stats;
        ctx->ring->GetStats(&stats);
        RtlCopyMemory(buffer, &stats, sizeof(stats));
        Irp->IoStatus.Information = sizeof(stats);
        return STATUS_SUCCESS;
    }

    default:
        *Handled = FALSE;
        return STATUS_NOT_SUPPORTED;
    }
}

static NTSTATUS SmartMicDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN handled = FALSE;

    const NTSTATUS status = SmartMicHandlePrivateIoctl(Irp, stack, &handled);
    if (!handled) {
        /* Not ours: hand it to PortCls untouched. */
        return g_PcDeviceControl(DeviceObject, Irp);
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS SmartMicClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    /* A service that crashed without unmapping must not leave the ring claimed
       forever, or a restart could never take it back (risk R11). */
    if (g_SmartMicContext != NULL && g_SmartMicContext->ring != NULL) {
        g_SmartMicContext->ring->ReleaseClaimIfOwner(PsGetCurrentProcess());
    }
    return g_PcClose(DeviceObject, Irp);
}

/*++
    StartDevice: PortCls calls this once the PnP manager has started us.
--*/
static NTSTATUS SmartMicStartDevice(_In_ PDEVICE_OBJECT DeviceObject,
                                    _In_ PIRP Irp,
                                    _In_ PRESOURCELIST ResourceList)
{
    PAGED_CODE();
    SmTrace("StartDevice");
    NTSTATUS status = SmartMicInstallSubdevices(DeviceObject, Irp, ResourceList);
    if (NT_SUCCESS(status) && g_SmartMicContext != NULL && g_SmartMicContext->interfaceRegistered) {
        IoSetDeviceInterfaceState(&g_SmartMicContext->interfaceName, TRUE);
    }
    return status;
}

static NTSTATUS SmartMicPnpHandler(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->MinorFunction == IRP_MN_REMOVE_DEVICE ||
        stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL) {
        if (g_SmartMicContext != NULL) {
            if (g_SmartMicContext->interfaceRegistered) {
                IoSetDeviceInterfaceState(&g_SmartMicContext->interfaceName, FALSE);
                RtlFreeUnicodeString(&g_SmartMicContext->interfaceName);
                g_SmartMicContext->interfaceRegistered = FALSE;
            }
            if (g_SmartMicContext->ring != NULL) {
                g_SmartMicContext->ring->Cleanup();
                delete g_SmartMicContext->ring;
                g_SmartMicContext->ring = NULL;
            }
            ExFreePoolWithTag(g_SmartMicContext, SMARTMIC_POOLTAG);
            g_SmartMicContext = NULL;
        }
    }
    return PcDispatchIrp(DeviceObject, Irp);
}

extern "C" NTSTATUS SmartMicAddDevice(_In_ PDRIVER_OBJECT DriverObject,
                                      _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PAGED_CODE();
    SmTrace("AddDevice");

    /* A virtual audio device has exactly one instance. If a second one ever
       appears, refuse rather than quietly sharing global state. */
    if (g_SmartMicContext != NULL) {
        SmTrace("refusing a second device instance");
        return STATUS_DEVICE_ALREADY_ATTACHED;
    }

    PSMARTMIC_DEVICE_CONTEXT ctx = (PSMARTMIC_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(SMARTMIC_DEVICE_CONTEXT), SMARTMIC_POOLTAG);
    if (ctx == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ctx, sizeof(*ctx));

    void* ringMem = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CSmartMicRing), SMARTMIC_POOLTAG);
    if (ringMem == NULL) {
        ExFreePoolWithTag(ctx, SMARTMIC_POOLTAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ctx->ring = new (ringMem) CSmartMicRing();

    NTSTATUS status = ctx->ring->Initialize();
    if (!NT_SUCCESS(status)) {
        delete ctx->ring;
        ExFreePoolWithTag(ctx, SMARTMIC_POOLTAG);
        return status;
    }

    /* maxObjects = 2: one wave miniport, one topology miniport. */
    status = PcAddAdapterDevice(DriverObject,
                                PhysicalDeviceObject,
                                (PCPFNSTARTDEVICE)SmartMicStartDevice,
                                2,
                                0);
    if (!NT_SUCCESS(status)) {
        delete ctx->ring;
        ExFreePoolWithTag(ctx, SMARTMIC_POOLTAG);
        return status;
    }

    status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DEVINTERFACE_SMARTMIC,
                                       NULL,
                                       &ctx->interfaceName);
    if (NT_SUCCESS(status)) {
        ctx->interfaceRegistered = TRUE;
    } else {
        /* The endpoint still works; only the service's private channel is
           unavailable, which the service reports as "repair needed". */
        SmTrace("IoRegisterDeviceInterface failed: 0x%08X", status);
    }

    g_SmartMicContext = ctx;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
                                _In_ PUNICODE_STRING RegistryPath)
{
    PAGED_CODE();

    NTSTATUS status = PcInitializeAdapterDriver(DriverObject,
                                                RegistryPath,
                                                SmartMicAddDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    /* Chain, do not replace: PortCls must keep seeing everything it needs. */
    g_PcDeviceControl = DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    g_PcClose         = DriverObject->MajorFunction[IRP_MJ_CLOSE];
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SmartMicDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = SmartMicClose;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = SmartMicPnpHandler;

    SmTrace("DriverEntry ok, version %08X", SMARTMIC_DRIVER_VERSION);
    return STATUS_SUCCESS;
}

#pragma code_seg()
