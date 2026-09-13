#include <initguid.h>
#include "smartmic_driver_link.h"

#include <setupapi.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "smartmic/logging.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "mincore.lib")

namespace smartmic {
namespace {
constexpr char kComponent[] = "DriverLink";

// Helper: narrow a wide string for logging.
static std::string narrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    std::string out(ws.begin(), ws.end()); // lossy but fine for ASCII device paths
    return out;
}

// ---- Method 1: SetupDi with DIGCF_PRESENT (standard, requires enabled interface) ----
std::wstring findViaSetupDi() {
    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_SMARTMIC, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return {};

    std::wstring result;
    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_SMARTMIC, i,
                                                  &ifData); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        std::vector<uint8_t> buffer(needed);
        auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, needed, nullptr, nullptr)) {
            result = detail->DevicePath;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

// ---- Method 2: CM_Get_Device_Interface_List (finds even disabled interfaces) ----
std::wstring findViaCM() {
    ULONG bufSize = 0;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &bufSize, const_cast<LPGUID>(&GUID_DEVINTERFACE_SMARTMIC), nullptr,
        CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
    if (cr != CR_SUCCESS || bufSize <= 1) return {};

    std::vector<wchar_t> buf(bufSize);
    cr = CM_Get_Device_Interface_ListW(
        const_cast<LPGUID>(&GUID_DEVINTERFACE_SMARTMIC), nullptr,
        buf.data(), bufSize, CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES);
    if (cr == CR_SUCCESS && buf[0] != L'\0') {
        return std::wstring(buf.data());
    }
    return {};
}

// ---- Diagnostic: check if the device node exists and whether it is started ----
void diagnoseDeviceNode() {
    // Look for any device with enumerator "ROOT" and hardware ID containing "smartmic"
    HDEVINFO devSet = SetupDiGetClassDevsW(nullptr, L"ROOT\\SMARTMIC", nullptr,
                                           DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devSet == INVALID_HANDLE_VALUE) {
        // Try without DIGCF_PRESENT to see if it exists but is not started
        devSet = SetupDiGetClassDevsW(nullptr, L"ROOT\\SMARTMIC", nullptr, DIGCF_ALLCLASSES);
    }
    if (devSet == INVALID_HANDLE_VALUE) {
        logError(kComponent, "DIAG: Cannot enumerate devices at all (SetupDi error).");
        return;
    }

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(devInfo);
    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devSet, i, &devInfo); ++i) {
        found = true;
        logInfo(kComponent, "DIAG: SmartMic device node FOUND in Device Manager.");

        ULONG status = 0, problem = 0;
        CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, devInfo.DevInst, 0);
        if (cr == CR_SUCCESS) {
            if (status & DN_STARTED) {
                logInfo(kComponent, "DIAG: Driver IS loaded and running (DN_STARTED).");
                logError(kComponent, "DIAG: But the control interface GUID is not registered.");
                logError(kComponent, "DIAG: The driver .sys may be an older version that does not");
                logError(kComponent, "DIAG: call IoRegisterDeviceInterface. Reinstall the latest driver.");
            } else if (status & DN_HAS_PROBLEM) {
                logError(kComponent, "DIAG: Driver has a PROBLEM. Code = " + std::to_string(problem));
                if (problem == 52) {
                    logError(kComponent, "DIAG: Problem 52 = driver signature enforcement is blocking it.");
                    logError(kComponent, "DIAG: FIX: Open an admin Command Prompt and run:");
                    logError(kComponent, "DIAG:   bcdedit /set testsigning on");
                    logError(kComponent, "DIAG: Then REBOOT. (Requires Secure Boot to be off in BIOS.)");
                } else if (problem == 31) {
                    logError(kComponent, "DIAG: Problem 31 = device not working properly.");
                    logError(kComponent, "DIAG: Try uninstalling and reinstalling the driver.");
                } else {
                    logError(kComponent, "DIAG: Look up CM_PROB code " + std::to_string(problem) + " in Microsoft docs.");
                }
            } else {
                logError(kComponent, "DIAG: Device exists but is NOT started. Status flags = 0x"
                                     + std::to_string(status));
            }
        } else {
            logError(kComponent, "DIAG: Could not query device status (CM error " + std::to_string(cr) + ").");
        }
    }

    if (!found) {
        logError(kComponent, "DIAG: SmartMic device node NOT FOUND in Device Manager.");
        logError(kComponent, "DIAG: The driver is not installed. Install it via Device Manager:");
        logError(kComponent, "DIAG:   Action > Add legacy hardware > Install from disk > smartmic.inf");
    }

    SetupDiDestroyDeviceInfoList(devSet);
}

// Main entry point: try every method, then diagnose.
std::wstring findDevicePath() {
    // Method 1: standard SetupDi (interface must be present and enabled)
    {
        std::wstring path = findViaSetupDi();
        if (!path.empty()) {
            logInfo(kComponent, "Found driver via SetupDi: " + narrow(path));
            return path;
        }
    }

    // Method 2: CM API (finds even disabled/not-yet-enabled interfaces)
    {
        std::wstring path = findViaCM();
        if (!path.empty()) {
            logInfo(kComponent, "Found driver via CM (possibly disabled interface): " + narrow(path));
            return path;
        }
    }

    // Neither method found anything. Run diagnostics to explain why.
    logError(kComponent, "SmartMic driver interface not found. Running diagnostics...");
    diagnoseDeviceNode();
    return {};
}
}  // namespace

SmartMicDriverLink::SmartMicDriverLink(std::string id) : id_(std::move(id)) {}
SmartMicDriverLink::~SmartMicDriverLink() { stop(); }

bool SmartMicDriverLink::openDevice() {
    const std::wstring path = findDevicePath();
    if (path.empty()) {
        logError(kComponent, "Cannot proceed without the SmartMic driver.");
        return false;
    }
    device_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device_ == INVALID_HANDLE_VALUE) {
        logError(kComponent, "CreateFile failed on: " + narrow(path) +
                             " (error " + std::to_string(GetLastError()) + ")");
        return false;
    }
    return true;
}

void SmartMicDriverLink::closeDevice() {
    if (device_ != INVALID_HANDLE_VALUE) {
        DWORD returned = 0;
        DeviceIoControl(device_, IOCTL_SMARTMIC_UNMAP_RING, nullptr, 0, nullptr, 0, &returned, nullptr);
        CloseHandle(device_);
        device_ = INVALID_HANDLE_VALUE;
    }
    ring_ = nullptr;
    payload_ = nullptr;
}

bool SmartMicDriverLink::start() {
    if (ring_ != nullptr) return true;
    if (!openDevice()) return false;

    DWORD returned = 0;
    SMARTMIC_VERSION_INFO version{};
    if (!DeviceIoControl(device_, IOCTL_SMARTMIC_GET_VERSION, nullptr, 0,
                         &version, sizeof(version), &returned, nullptr) ||
        returned != sizeof(version)) {
        logError(kComponent, "GET_VERSION failed");
        closeDevice();
        return false;
    }
    driverVersion_ = version.driverVersion;

    // Refuse a ring layout we were not compiled against. Silently coping with a
    // mismatch here would mean writing audio into the wrong offsets.
    if (version.ringVersion != SMARTMIC_RING_VERSION ||
        version.ringTotalBytes != SMARTMIC_RING_TOTAL_BYTES) {
        logError(kComponent, "driver/service ring mismatch: driver ring v" +
                                 std::to_string(version.ringVersion) + ", service expects v" +
                                 std::to_string(SMARTMIC_RING_VERSION) +
                                 " -- reinstall so both halves match");
        closeDevice();
        return false;
    }

    SMARTMIC_MAP_RESULT map{};
    if (!DeviceIoControl(device_, IOCTL_SMARTMIC_MAP_RING, nullptr, 0,
                         &map, sizeof(map), &returned, nullptr) ||
        returned != sizeof(map) || map.baseAddress == nullptr) {
        logError(kComponent, "MAP_RING failed -- another SmartMic service may be running");
        closeDevice();
        return false;
    }

    ring_ = reinterpret_cast<SMARTMIC_RING_HEADER*>(map.baseAddress);
    if (ring_->magic != SMARTMIC_RING_MAGIC) {
        logError(kComponent, "ring magic mismatch");
        closeDevice();
        return false;
    }
    payload_ = reinterpret_cast<uint8_t*>(map.baseAddress) + ring_->payloadOffset;

    beat();
    logInfo(kComponent, "attached to SmartMic driver, version " + std::to_string(driverVersion_));
    return true;
}

void SmartMicDriverLink::stop() { closeDevice(); }

void SmartMicDriverLink::beat() {
    if (ring_ == nullptr) return;
    // The driver treats a stale heartbeat as "the service is gone" and emits
    // silence. Updating it on every write means a wedged service goes quiet
    // within 250 ms without anyone having to notice and act.
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    ULONGLONG interruptTime = 0;
    QueryInterruptTime(&interruptTime);
    ring_->producerHeartbeat = interruptTime;
}

bool SmartMicDriverLink::write(const float* in, size_t samples) {
    if (ring_ == nullptr || payload_ == nullptr) return false;

    // Canonical float -> the driver's fixed PCM16, with a hard limit. The
    // driver stays dumb; conversion is our job (ADR-002).
    scratch_.resize(samples);
    for (size_t i = 0; i < samples; ++i) {
        const float v = std::clamp(in[i], -0.999f, 0.999f);
        scratch_[i] = static_cast<int16_t>(std::lround(v * 32767.0f));
    }

    const uint64_t write = ring_->writeIndex;
    const uint64_t read = ring_->readIndex;
    const uint64_t used = write - read;

    if (used + samples > SMARTMIC_RING_FRAMES) {
        // The driver is not draining (no stream open, or a stalled consumer).
        // Dropping here rather than blocking keeps the router's timing exact.
        ring_->overrunCount += static_cast<uint32_t>(samples);
        beat();
        return true;
    }

    const size_t offset = static_cast<size_t>(write & SMARTMIC_RING_MASK);
    const size_t first = std::min(samples, static_cast<size_t>(SMARTMIC_RING_FRAMES) - offset);
    std::memcpy(payload_ + offset * SMARTMIC_BYTES_PER_FRAME, scratch_.data(),
                first * sizeof(int16_t));
    if (samples > first) {
        std::memcpy(payload_, scratch_.data() + first, (samples - first) * sizeof(int16_t));
    }

    // Publish the payload before the index: the driver acquires writeIndex and
    // then reads what it points at.
    MemoryBarrier();
    ring_->writeIndex = write + samples;
    beat();
    return true;
}

uint64_t SmartMicDriverLink::underrunCount() const {
    return ring_ ? ring_->underrunCount : 0;
}
uint64_t SmartMicDriverLink::overrunCount() const {
    return ring_ ? ring_->overrunCount : 0;
}
uint64_t SmartMicDriverLink::framesDelivered() const {
    return ring_ ? ring_->framesDelivered : 0;
}
bool SmartMicDriverLink::streamOpen() const {
    return ring_ && ring_->streamOpen != 0;
}

}  // namespace smartmic
