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

// Finds the SmartMic control interface exposed by the driver.
std::wstring findDevicePath() {
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
}  // namespace

SmartMicDriverLink::SmartMicDriverLink(std::string id) : id_(std::move(id)) {}
SmartMicDriverLink::~SmartMicDriverLink() { stop(); }

bool SmartMicDriverLink::openDevice() {
    const std::wstring path = findDevicePath();
    if (path.empty()) {
        logError(kComponent, "SmartMic driver interface not found -- is the driver installed?");
        return false;
    }
    device_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device_ == INVALID_HANDLE_VALUE) {
        logError(kComponent, "cannot open the SmartMic device");
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
