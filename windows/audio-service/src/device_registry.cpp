#include "smartmic/device_registry.h"

#include <algorithm>

#include "smartmic/logging.h"

namespace smartmic {

void DeviceRegistry::setDevices(std::vector<DeviceInfo> devices) {
    ChangeCallback cb;
    {
        std::lock_guard<std::mutex> lk(mu_);

        std::vector<std::string> previousIds;
        previousIds.reserve(devices_.size());
        for (const auto& d : devices_) previousIds.push_back(d.id);

        devices_ = std::move(devices);

        // Auto-prefer a newly arrived usable device, if the user asked for it.
        if (autoPrefer_) {
            for (const auto& d : devices_) {
                if (d.isSmartMicEndpoint) continue;
                const bool isNew = std::find(previousIds.begin(), previousIds.end(), d.id) == previousIds.end();
                if (isNew && !previousIds.empty()) {
                    preferredId_ = d.id;
                    break;
                }
            }
        }
        cb = onChanged_;
    }
    if (cb) cb();
}

std::vector<DeviceInfo> DeviceRegistry::allDevices() const {
    std::lock_guard<std::mutex> lk(mu_);
    return devices_;
}

std::vector<DeviceInfo> DeviceRegistry::availableInputs() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<DeviceInfo> out;
    out.reserve(devices_.size());
    for (const auto& d : devices_) {
        // Guard #1: SmartMic's own endpoint is never a candidate input.
        if (d.isSmartMicEndpoint) continue;
        out.push_back(d);
    }
    return out;
}

bool DeviceRegistry::setPreferredInput(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& d : devices_) {
        if (d.id != id) continue;
        // Guard #2: refuse explicitly, loudly. If this ever fires, something
        // upstream is trying to make SmartMic capture itself (risk R12).
        if (d.isSmartMicEndpoint) {
            logError("DeviceRegistry", "refused to select the SmartMic endpoint as a capture source");
            return false;
        }
        preferredId_ = id;
        return true;
    }
    return false;
}

std::optional<std::string> DeviceRegistry::preferredInputId() const {
    std::lock_guard<std::mutex> lk(mu_);
    return preferredId_;
}

std::optional<DeviceInfo> DeviceRegistry::preferredInput() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!preferredId_) return std::nullopt;
    for (const auto& d : devices_) {
        if (d.id == *preferredId_ && !d.isSmartMicEndpoint) return d;
    }
    return std::nullopt;
}

std::optional<DeviceInfo> DeviceRegistry::resolveEffectiveInput() const {
    std::lock_guard<std::mutex> lk(mu_);
    if (preferredId_) {
        for (const auto& d : devices_) {
            if (d.id == *preferredId_ && !d.isSmartMicEndpoint) return d;
        }
    }
    for (const auto& d : devices_) {
        if (!d.isSmartMicEndpoint && d.isDefault) return d;
    }
    for (const auto& d : devices_) {
        if (!d.isSmartMicEndpoint) return d;
    }
    return std::nullopt;
}

void DeviceRegistry::setAutoPreferNewDevices(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    autoPrefer_ = on;
}

bool DeviceRegistry::autoPreferNewDevices() const {
    std::lock_guard<std::mutex> lk(mu_);
    return autoPrefer_;
}

void DeviceRegistry::onChanged(ChangeCallback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    onChanged_ = std::move(cb);
}

}  // namespace smartmic
