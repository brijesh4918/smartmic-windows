#include "smartmic/device_registry.h"
#include "test_harness.h"

using namespace smartmic;

namespace {
std::vector<DeviceInfo> inventory() {
    DeviceInfo realtek{"{realtek}", "Microphone Array (Realtek)", true, false, 48000, 2};
    DeviceInfo jabra{"{jabra}", "Jabra Evolve2", false, false, 16000, 1};
    DeviceInfo smart{"{smartmic}", "Smart Microphone", false, true, 48000, 1};
    return {realtek, jabra, smart};
}
}  // namespace

SM_TEST(A12, "SmartMic's own endpoint can never be chosen as a capture source") {
    DeviceRegistry reg;
    reg.setDevices(inventory());

    // Guard #1: it is not offered.
    const auto inputs = reg.availableInputs();
    CHECK_EQ(inputs.size(), 2u);
    for (const auto& d : inputs) CHECK(!d.isSmartMicEndpoint);

    // Guard #2: asking for it directly is refused.
    CHECK(!reg.setPreferredInput("{smartmic}"));
    CHECK(!reg.preferredInputId().has_value());

    // A real device is accepted.
    CHECK(reg.setPreferredInput("{jabra}"));
    CHECK(reg.preferredInput().has_value());
    CHECK(reg.preferredInput()->id == std::string("{jabra}"));

    // Guard #3: even when SmartMic is the Windows default, resolution skips it.
    auto devices = inventory();
    for (auto& d : devices) d.isDefault = d.isSmartMicEndpoint;
    reg.setDevices(devices);
    CHECK(reg.resolveEffectiveInput().has_value());
    CHECK(!reg.resolveEffectiveInput()->isSmartMicEndpoint);
}

SM_TEST(A12b, "unplugging the preferred device falls back, and replugging restores it") {
    DeviceRegistry reg;
    reg.setDevices(inventory());
    CHECK(reg.setPreferredInput("{jabra}"));

    std::vector<DeviceInfo> withoutJabra{inventory()[0], inventory()[2]};
    reg.setDevices(withoutJabra);
    CHECK(!reg.preferredInput().has_value());              // preference remembered...
    CHECK(reg.resolveEffectiveInput()->id == std::string("{realtek}"));  // ...but not used

    reg.setDevices(inventory());
    CHECK(reg.resolveEffectiveInput()->id == std::string("{jabra}"));
}

SM_TEST(A12c, "auto-prefer picks up a newly connected headset, never SmartMic") {
    DeviceRegistry reg;
    reg.setAutoPreferNewDevices(true);
    std::vector<DeviceInfo> initial{inventory()[0], inventory()[2]};
    reg.setDevices(initial);
    CHECK(!reg.preferredInputId().has_value());

    reg.setDevices(inventory());   // Jabra arrives
    CHECK(reg.preferredInputId().has_value());
    CHECK(*reg.preferredInputId() == std::string("{jabra}"));
}
