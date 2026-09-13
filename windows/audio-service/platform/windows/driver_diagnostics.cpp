#include "driver_diagnostics.h"

#include <windows.h>

#include <setupapi.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <vector>

#include "wasapi_common.h"

#include "../../../driver/inc/smartmic_ring.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace smartmic {
namespace {

// Is the kernel service registered, and is it running? This is the cleanest
// signal available from user mode: a driver package that installed correctly
// leaves a service behind, and a driver that actually loaded leaves it RUNNING.
void queryService(bool& registered, bool& running) {
    registered = false;
    running = false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) return;

    SC_HANDLE svc = OpenServiceW(scm, L"smartmic", SERVICE_QUERY_STATUS);
    if (svc == nullptr) {
        // ERROR_SERVICE_DOES_NOT_EXIST is the interesting case and the default.
        CloseServiceHandle(scm);
        return;
    }
    registered = true;

    SERVICE_STATUS status{};
    if (QueryServiceStatus(svc, &status)) {
        running = (status.dwCurrentState == SERVICE_RUNNING);
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

bool queryInterfacePresent() {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_SMARTMIC, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA data{};
    data.cbSize = sizeof(data);
    const bool found =
        SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_SMARTMIC, 0, &data) != FALSE;
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

// The question a user actually cares about: does a microphone called "Smart
// Microphone" show up for Teams and Zoom? The control interface being present
// does not guarantee it -- the audio filters are a separate part of the driver.
bool queryAudioEndpoint(std::string& nameOut) {
    // The diagnose path may run before anything else has initialised COM.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool weInitialised = SUCCEEDED(init);

    bool found = false;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), enumerator.putVoid()))) {
        ComPtr<IMMDeviceCollection> collection;
        // DEVICE_STATE_UNPLUGGED and _DISABLED are included deliberately: an
        // endpoint that exists but is disabled is a completely different
        // problem from one that was never created.
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(
                eCapture,
                DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED,
                collection.put()))) {
            UINT count = 0;
            collection->GetCount(&count);
            for (UINT i = 0; i < count && !found; ++i) {
                ComPtr<IMMDevice> device;
                if (FAILED(collection->Item(i, device.put()))) continue;

                std::string id, name;
                LPWSTR rawId = nullptr;
                if (SUCCEEDED(device->GetId(&rawId))) {
                    id = wideToUtf8(rawId);
                    CoTaskMemFree(rawId);
                }
                ComPtr<IPropertyStore> props;
                if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put()))) {
                    PROPVARIANT v;
                    PropVariantInit(&v);
                    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) &&
                        v.vt == VT_LPWSTR) {
                        name = wideToUtf8(v.pwszVal);
                    }
                    PropVariantClear(&v);
                }
                if (looksLikeSmartMicEndpoint(id, name)) {
                    found = true;
                    nameOut = name;
                }
            }
        }
    }

    if (weInitialised) CoUninitialize();
    return found;
}

}  // namespace

DriverDiagnosis diagnoseDriver() {
    DriverDiagnosis d;
    queryService(d.packageInstalled, d.driverLoaded);
    d.interfacePresent = queryInterfacePresent();
    d.audioEndpointPresent = queryAudioEndpoint(d.endpointName);

    if (d.interfacePresent && d.audioEndpointPresent) {
        d.summary = "the SmartMic driver is installed and running";
        d.advice = "";
        return d;
    }

    if (d.interfacePresent && !d.audioEndpointPresent) {
        // The control half works but the audio half did not materialise.
        d.summary = "the driver is running, but Windows is not publishing a "
                    "'Smart Microphone' capture device";
        d.advice = "the audio filters did not register. Check Device Manager for "
                   "a warning on 'SmartMic Virtual Audio Device', and check the "
                   "device instance id -- it should be ROOT\\SMARTMIC\\0000; "
                   "ROOT\\UNKNOWN\\0000 means the INF did not match and the device "
                   "was created as an unknown device";
        return d;
    }

    if (!d.packageInstalled) {
        d.summary = "the SmartMic driver is not installed on this PC";
        d.advice = "run install-driver.bat as Administrator, from the folder "
                   "containing smartmic.sys";
        return d;
    }

    if (!d.driverLoaded) {
        // This is the interesting one, and almost always means the signature
        // was rejected: the package staged fine, so Windows knows about it, but
        // the kernel refused to load the binary.
        d.summary = "the SmartMic driver is installed but Windows refused to load it";
        d.advice = "this is nearly always a signing problem: check Device Manager "
                   "for a yellow warning on 'SmartMic Virtual Audio Device' "
                   "(code 52 means the signature was rejected), confirm test "
                   "signing is on with 'bcdedit /enum {current}', and make sure "
                   "SmartMicTestCert.cer was imported before installing";
        return d;
    }

    d.summary = "the SmartMic driver is loaded but its control interface is not available";
    d.advice = "another SmartMic service may already be attached to it; close any "
               "other copy and try again";
    return d;
}

std::string formatDriverDiagnosis(const DriverDiagnosis& d) {
    std::string out;
    out += "  driver package installed : ";
    out += d.packageInstalled ? "yes\n" : "NO\n";
    out += "  driver loaded by Windows : ";
    out += d.driverLoaded ? "yes\n" : "NO\n";
    out += "  control interface present: ";
    out += d.interfacePresent ? "yes\n" : "NO\n";
    out += "  'Smart Microphone' endpoint: ";
    if (d.audioEndpointPresent) {
        out += "yes";
        if (!d.endpointName.empty()) { out += " ("; out += d.endpointName; out += ")"; }
        out += "\n";
    } else {
        out += "NO\n";
    }
    out += "\n  ";
    out += d.summary;
    out += "\n";
    if (!d.advice.empty()) {
        out += "  -> ";
        out += d.advice;
        out += "\n";
    }
    return out;
}

}  // namespace smartmic
