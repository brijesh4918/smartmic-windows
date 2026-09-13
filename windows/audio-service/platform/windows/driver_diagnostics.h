// Why is there no "Smart Microphone"?
//
// "Driver interface not found" is true but useless: it cannot tell the
// difference between a driver that was never installed, one that is installed
// but refused to load because it is unsigned, and one that loaded fine but
// whose control interface is disabled. Each has a different fix, so the
// service works out which one it is and says so.
#pragma once

#include <string>

namespace smartmic {

struct DriverDiagnosis {
    bool packageInstalled = false;   // the smartmic service is registered
    bool driverLoaded = false;       // ...and the kernel service is running
    bool interfacePresent = false;   // ...and the control interface is enabled
    std::string summary;             // one line: what is wrong
    std::string advice;              // one line: what to do about it
};

DriverDiagnosis diagnoseDriver();

// Multi-line, ready to print.
std::string formatDriverDiagnosis(const DriverDiagnosis& d);

}  // namespace smartmic
