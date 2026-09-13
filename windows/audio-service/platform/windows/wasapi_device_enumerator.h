#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "smartmic/audio_io.h"
#include "wasapi_common.h"

namespace smartmic {

// Enumerates endpoints and reports hot-plug / default-device changes via
// IMMNotificationClient. It deliberately does NOT ask Windows what the default
// capture device is in order to choose a source: once SmartMic is the default,
// that question answers itself and the router would capture its own output
// (risk R12). The preferred device is SmartMic's own setting, seeded by the
// installer from the pre-install default.
class WasapiDeviceEnumerator {
public:
    WasapiDeviceEnumerator();
    ~WasapiDeviceEnumerator();

    bool initialise();
    void shutdown();

    std::vector<DeviceInfo> enumerateCapture();
    std::vector<DeviceInfo> enumerateRender();

    void onDevicesChanged(std::function<void()> cb);

private:
    class NotificationClient;

    std::vector<DeviceInfo> enumerate(EDataFlow flow);

    ComPtr<IMMDeviceEnumerator> enumerator_;
    NotificationClient* notifications_ = nullptr;
    std::mutex mu_;
    std::function<void()> onChanged_;
    bool comInitialised_ = false;
};

}  // namespace smartmic
