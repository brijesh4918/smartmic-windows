#include "driver_diagnostics.h"

#include <windows.h>

#include <setupapi.h>
#include <vector>

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

}  // namespace

DriverDiagnosis diagnoseDriver() {
    DriverDiagnosis d;
    queryService(d.packageInstalled, d.driverLoaded);
    d.interfacePresent = queryInterfacePresent();

    if (d.interfacePresent) {
        d.summary = "the SmartMic driver is installed and running";
        d.advice = "";
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
