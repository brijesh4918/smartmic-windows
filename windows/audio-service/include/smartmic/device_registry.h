// Inventory of physical capture devices, plus the guard that stops SmartMic
// from ever selecting itself as its own input (ADR-001, risk R12, test A12).
#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "smartmic/audio_io.h"

namespace smartmic {

class DeviceRegistry {
public:
    using ChangeCallback = std::function<void()>;

    // Replaces the whole inventory (how a platform enumerator reports a
    // hot-plug). SmartMic's own endpoint is retained for display but is never
    // returned by availableInputs().
    void setDevices(std::vector<DeviceInfo> devices);

    std::vector<DeviceInfo> allDevices() const;
    std::vector<DeviceInfo> availableInputs() const;

    // Returns false and changes nothing if `id` is unknown or is the SmartMic
    // endpoint. This is the second of the two guards; the first is the filter
    // in availableInputs().
    bool setPreferredInput(const std::string& id);

    std::optional<std::string> preferredInputId() const;
    std::optional<DeviceInfo> preferredInput() const;

    // Chooses a reasonable input when the preferred one is absent: the system
    // default if it is usable, else the first usable device, else nothing.
    std::optional<DeviceInfo> resolveEffectiveInput() const;

    void setAutoPreferNewDevices(bool on);
    bool autoPreferNewDevices() const;

    void onChanged(ChangeCallback cb);

private:
    mutable std::mutex mu_;
    std::vector<DeviceInfo> devices_;
    std::optional<std::string> preferredId_;
    bool autoPrefer_ = false;
    ChangeCallback onChanged_;
};

}  // namespace smartmic
