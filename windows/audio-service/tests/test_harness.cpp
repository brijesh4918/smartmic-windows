#include "test_harness.h"

#include <exception>
#include <stdexcept>

namespace smartmic::test {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void fail(const char* file, int line, const std::string& what) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + " " + what);
}

int runAll() {
    int failed = 0;
    for (const auto& c : registry()) {
        try {
            c.fn();
            std::printf("  ok   %-8s %s\n", c.id.c_str(), c.name.c_str());
        } catch (const std::exception& e) {
            std::printf("  FAIL %-8s %s\n         %s\n", c.id.c_str(), c.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace smartmic::test

int main() {
    std::printf("SmartMic Phase 1 acceptance tests\n\n");
    return smartmic::test::runAll();
}
