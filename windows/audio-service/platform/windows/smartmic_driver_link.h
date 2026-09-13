// User-mode half of the driver interface: the Phase 5 replacement for
// WasapiRenderSink (ADR-001). The router cannot tell them apart.
#pragma once

#include <atomic>
#include <string>

#include "smartmic/audio_io.h"
#include "wasapi_common.h"

// Byte-for-byte the same header the driver compiles.
#include "../../../driver/inc/smartmic_ring.h"

namespace smartmic {

class SmartMicDriverLink : public IDriverLink {
public:
    explicit SmartMicDriverLink(std::string id);
    ~SmartMicDriverLink() override;

    const std::string& id() const override { return id_; }
    bool start() override;
    void stop() override;
    bool write(const float* in, size_t samples) override;

    bool endpointReady() const override { return ring_ != nullptr; }
    uint64_t underrunCount() const override;
    uint64_t overrunCount() const;
    uint64_t framesDelivered() const;
    bool streamOpen() const;

    // Set once at start; surfaced by the desktop app so a driver/service
    // version mismatch is visible rather than mysterious.
    uint32_t driverVersion() const { return driverVersion_; }

private:
    bool openDevice();
    void closeDevice();
    void beat();

    std::string id_;
    HANDLE device_ = INVALID_HANDLE_VALUE;
    SMARTMIC_RING_HEADER* ring_ = nullptr;
    uint8_t* payload_ = nullptr;
    uint32_t driverVersion_ = 0;
    std::vector<int16_t> scratch_;
};

}  // namespace smartmic
