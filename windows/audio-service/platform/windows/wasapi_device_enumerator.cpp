#include "wasapi_device_enumerator.h"

#include <functiondiscoverykeys_devpkey.h>

#include "smartmic/logging.h"

namespace smartmic {
namespace {
constexpr char kComponent[] = "WasapiEnumerator";
}

// IMMNotificationClient: Windows calls these from its own thread, so they only
// post a "something changed" signal. Re-enumeration happens on the caller's
// thread, never inside the notification.
class WasapiDeviceEnumerator::NotificationClient : public IMMNotificationClient {
public:
    explicit NotificationClient(std::function<void()> cb) : cb_(std::move(cb)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            AddRef();
            *ppv = this;
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { fire(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void fire() { if (cb_) cb_(); }
    LONG ref_ = 1;
    std::function<void()> cb_;
};

WasapiDeviceEnumerator::WasapiDeviceEnumerator() = default;
WasapiDeviceEnumerator::~WasapiDeviceEnumerator() { shutdown(); }

bool WasapiDeviceEnumerator::initialise() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comInitialised_ = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) comInitialised_ = false;  // someone else owns the apartment

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), enumerator_.putVoid());
    if (FAILED(hr)) {
        logError(kComponent, "CoCreateInstance(MMDeviceEnumerator) failed");
        return false;
    }

    notifications_ = new NotificationClient([this] {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cb = onChanged_;
        }
        if (cb) cb();
    });
    enumerator_->RegisterEndpointNotificationCallback(notifications_);
    return true;
}

void WasapiDeviceEnumerator::shutdown() {
    if (enumerator_ && notifications_) {
        enumerator_->UnregisterEndpointNotificationCallback(notifications_);
        notifications_->Release();
        notifications_ = nullptr;
    }
    enumerator_.reset();
    if (comInitialised_) {
        CoUninitialize();
        comInitialised_ = false;
    }
}

std::vector<DeviceInfo> WasapiDeviceEnumerator::enumerate(EDataFlow flow) {
    std::vector<DeviceInfo> out;
    if (!enumerator_) return out;

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put()))) return out;

    // The system default is recorded for display only. Nothing in the routing
    // path ever selects a source because Windows called it the default.
    std::string defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(flow, eCommunications, defaultDevice.put()))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(defaultDevice->GetId(&id))) {
            defaultId = wideToUtf8(id);
            CoTaskMemFree(id);
        }
    }

    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.put()))) continue;

        DeviceInfo info;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id))) {
            info.id = wideToUtf8(id);
            CoTaskMemFree(id);
        }

        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.put()))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) {
                info.name = wideToUtf8(v.pwszVal);
            }
            PropVariantClear(&v);
        }

        ComPtr<IAudioClient> client;
        if (SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.putVoid()))) {
            WAVEFORMATEX* mix = nullptr;
            if (SUCCEEDED(client->GetMixFormat(&mix)) && mix) {
                info.nativeRate = mix->nSamplesPerSec;
                info.nativeChannels = mix->nChannels;
                CoTaskMemFree(mix);
            }
        }

        info.isDefault = !defaultId.empty() && info.id == defaultId;
        info.isSmartMicEndpoint = looksLikeSmartMicEndpoint(info.id, info.name);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<DeviceInfo> WasapiDeviceEnumerator::enumerateCapture() { return enumerate(eCapture); }
std::vector<DeviceInfo> WasapiDeviceEnumerator::enumerateRender() { return enumerate(eRender); }

void WasapiDeviceEnumerator::onDevicesChanged(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mu_);
    onChanged_ = std::move(cb);
}

}  // namespace smartmic
