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
static PDRIVER_DISPATCH g_PcCreate = NULL;
static PDRIVER_DISPATCH g_PcCleanup = NULL;

/* Stamped on the file object of an open of our control interface, so close,
   cleanup and device-control can tell our handles from PortCls's without
   re-parsing names. */
#define SMARTMIC_FILE_TAG ((PVOID)(ULONG_PTR)0x534D4331)   /* 'SMC1' */

static BOOLEAN SmartMicIsOurFile(_In_ PIO_STACK_LOCATION Stack)
{
    return Stack->FileObject != NULL && Stack->FileObject->FsContext == SMARTMIC_FILE_TAG;
}

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

/*++
    PortCls dispatches IRP_MJ_CREATE by subdevice name and fails anything it
    does not recognise. Our control channel is opened by its reference string,
    so it is claimed here and never reaches PortCls.
--*/
static NTSTATUS SmartMicCreate(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT file = stack->FileObject;

    if (file != NULL && file->FileName.Buffer != NULL && file->FileName.Length > 0) {
        /* The name arrives as "\SmartMicControl"; compare with and without the
           leading separator rather than assuming which one we get. */
        UNICODE_STRING withSlash, bare;
        RtlInitUnicodeString(&withSlash, L"\\" SMARTMIC_CONTROL_REFERENCE);
        RtlInitUnicodeString(&bare, SMARTMIC_CONTROL_REFERENCE);

        if (RtlEqualUnicodeString(&file->FileName, &withSlash, TRUE) ||
            RtlEqualUnicodeString(&file->FileName, &bare, TRUE)) {
            file->FsContext = SMARTMIC_FILE_TAG;
            SmTrace("control channel opened");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = FILE_OPENED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;
        }
    }

    return g_PcCreate(DeviceObject, Irp);
}

static NTSTATUS SmartMicCleanup(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    if (SmartMicIsOurFile(stack)) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
    return g_PcCleanup != NULL ? g_PcCleanup(DeviceObject, Irp) : PcDispatchIrp(DeviceObject, Irp);
}

static NTSTATUS SmartMicDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    PAGED_CODE();

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN handled = FALSE;

    /* Only handles opened through our control interface may drive the ring. */
    if (!SmartMicIsOurFile(stack)) {
        return g_PcDeviceControl(DeviceObject, Irp);
    }

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

    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);

    /* A service that crashed without unmapping must not leave the ring claimed
       forever, or a restart could never take it back (risk R11). */
    if (g_SmartMicContext != NULL && g_SmartMicContext->ring != NULL) {
        g_SmartMicContext->ring->ReleaseClaimIfOwner(PsGetCurrentProcess());
    }

    if (SmartMicIsOurFile(stack)) {
        /* PortCls never saw the create, so it must not see the close. */
        stack->FileObject->FsContext = NULL;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
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

    UNICODE_STRING controlReference;
    RtlInitUnicodeString(&controlReference, SMARTMIC_CONTROL_REFERENCE);
    status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_DEVINTERFACE_SMARTMIC,
                                       &controlReference,
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
    g_PcCreate        = DriverObject->MajorFunction[IRP_MJ_CREATE];
    g_PcCleanup       = DriverObject->MajorFunction[IRP_MJ_CLEANUP];
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SmartMicDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = SmartMicClose;
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = SmartMicCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]        = SmartMicCleanup;
    DriverObject->MajorFunction[IRP_MJ_PNP]            = SmartMicPnpHandler;

    SmTrace("DriverEntry ok, version %08X", SMARTMIC_DRIVER_VERSION);
    return STATUS_SUCCESS;
}

#pragma code_seg()
