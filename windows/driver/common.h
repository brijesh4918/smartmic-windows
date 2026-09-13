/*++
    common.h -- shared declarations for SmartMic.Driver.

    Design rule for this whole directory (ADR-002): the driver presents one
    capture endpoint and drains a bounded ring. It parses nothing from the
    network, decodes nothing, decides nothing. Every buffer length and every
    index that came from user mode is treated as hostile.
--*/
#ifndef SMARTMIC_COMMON_H
#define SMARTMIC_COMMON_H

#include <ntddk.h>
#include <wdm.h>
#include <windef.h>
#include <mmreg.h>      /* WAVEFORMATEX -- must come before ksmedia.h/portcls.h */
#include <ks.h>         /* KS types used by portcls.h */
#include <ksmedia.h>    /* KSDATAFORMAT_WAVEFORMATEX, pin categories, jack types */
#include <unknown.h>
#include <portcls.h>
#include <stdunk.h>
#include <ksdebug.h>
#include <ntstrsafe.h>

/* --------------------------------------------------------------------------
   Compatibility shims for WDK 26100+
   -------------------------------------------------------------------------- */

/* PFNCREATEMINIPORT was removed from portcls.h in newer WDK versions. */
#ifndef PFNCREATEMINIPORT_DEFINED
typedef NTSTATUS (*PFNCREATEMINIPORT)(
    _Out_ PUNKNOWN* Unknown,
    _In_ REFCLSID ClassId,
    _In_opt_ PUNKNOWN UnknownOuter,
    _In_ POOL_TYPE PoolType,
    _In_ PUNKNOWN UnknownAdapter,
    _In_opt_ PVOID DeviceContext
);
#define PFNCREATEMINIPORT_DEFINED
#endif

/* PADAPTERCOMMON is no longer defined in newer WDK headers. */
#ifndef PADAPTERCOMMON
typedef IUnknown* PADAPTERCOMMON;
#endif

/* KSAUDFNAME_MICROPHONE may be absent from the kernel-mode ksmedia.h. */
#ifndef KSAUDFNAME_MICROPHONE
#define STATIC_KSAUDFNAME_MICROPHONE \
    0x2bc31d69, 0x96e3, 0x11d2, 0xac, 0x4c, 0x00, 0xc0, 0x4f, 0x8e, 0xfb, 0x68
DEFINE_GUIDSTRUCT("2BC31D69-96E3-11D2-AC4C-00C04F8EFB68", KSAUDFNAME_MICROPHONE);
#define KSAUDFNAME_MICROPHONE DEFINE_GUIDNAMED(KSAUDFNAME_MICROPHONE)
#endif

/* -------------------------------------------------------------------------- */

#include "inc/smartmic_ring.h"

#define SMARTMIC_POOLTAG        'MtmS'   /* "SmtM" reversed, as tags are read */
#define SMARTMIC_DRIVER_VERSION 0x00010000u

#define SMARTMIC_FRIENDLY_NAME  L"Smart Microphone"

/* Notification period the WaveRT stream runs at: 10 ms, which is the standard
   shared-mode period and divides the 20 ms canonical frame cleanly. */
#define SMARTMIC_NOTIFY_MS      10
#define SMARTMIC_NOTIFY_FRAMES  ((SMARTMIC_SAMPLE_RATE * SMARTMIC_NOTIFY_MS) / 1000)

/* The WaveRT buffer handed to the audio engine. Four notification periods is
   enough to absorb scheduling jitter without adding audible latency. */
#define SMARTMIC_WAVERT_PERIODS 4
#define SMARTMIC_WAVERT_FRAMES  (SMARTMIC_NOTIFY_FRAMES * SMARTMIC_WAVERT_PERIODS)
#define SMARTMIC_WAVERT_BYTES   (SMARTMIC_WAVERT_FRAMES * SMARTMIC_BYTES_PER_FRAME)

#if DBG
#define SmTrace(fmt, ...) \
    DbgPrintEx(DPFLTR_IHVAUDIO_ID, DPFLTR_INFO_LEVEL, "SmartMic: " fmt "\n", __VA_ARGS__)
#else
#define SmTrace(fmt, ...) ((void)0)
#endif

/* Forward declarations. */
class CSmartMicRing;

/* Per-adapter context, hung off the port device extension via
   PcGetDeviceContext-style storage held in the global below. A virtual audio
   driver services exactly one device instance, so a single global is honest
   here -- and it is asserted in AddDevice rather than assumed. */
typedef struct _SMARTMIC_DEVICE_CONTEXT {
    PDEVICE_OBJECT   deviceObject;
    CSmartMicRing*   ring;
    UNICODE_STRING   interfaceName;
    BOOLEAN          interfaceRegistered;
} SMARTMIC_DEVICE_CONTEXT, *PSMARTMIC_DEVICE_CONTEXT;

extern PSMARTMIC_DEVICE_CONTEXT g_SmartMicContext;

/* Subdevice factory entry points (adapter.cpp). */
NTSTATUS SmartMicInstallSubdevices(_In_ PDEVICE_OBJECT DeviceObject,
                                   _In_ PIRP Irp,
                                   _In_ PRESOURCELIST ResourceList);

/* Miniport creators. */
NTSTATUS CreateMiniportWaveRTSmartMic(_Out_ PUNKNOWN* Unknown, _In_ REFCLSID,
                                      _In_opt_ PUNKNOWN UnknownOuter,
                                      _In_ POOL_TYPE PoolType,
                                      _In_ PUNKNOWN UnknownAdapter,
                                      _In_opt_ PVOID DeviceContext);

NTSTATUS CreateMiniportTopologySmartMic(_Out_ PUNKNOWN* Unknown, _In_ REFCLSID,
                                        _In_opt_ PUNKNOWN UnknownOuter,
                                        _In_ POOL_TYPE PoolType,
                                        _In_ PUNKNOWN UnknownAdapter,
                                        _In_opt_ PVOID DeviceContext);

#endif /* SMARTMIC_COMMON_H */
