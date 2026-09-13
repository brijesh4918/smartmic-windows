// Dependency-free test harness. No gtest: Phase 1 must build and run its tests
// on a bare toolchain, on any platform, with no package manager.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace smartmic::test {

struct Case {
    std::string id;
    std::string name;
    std::function<void()> fn;
};

std::vector<Case>& registry();
int runAll();
void fail(const char* file, int line, const std::string& what);

struct Registrar {
    Registrar(std::string id, std::string name, std::function<void()> fn) {
        registry().push_back({std::move(id), std::move(name), std::move(fn)});
    }
};

}  // namespace smartmic::test

#define SM_TEST(id, name)                                                             \
    static void id##_body();                                                          \
    static ::smartmic::test::Registrar id##_reg(#id, name, id##_body);                \
    static void id##_body()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) ::smartmic::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");   \
    } while (0)

#define CHECK_EQ(a, b)                                                                \
    do {                                                                              \
        auto _a = (a);                                                                \
        auto _b = (b);                                                                \
        if (!(_a == _b))                                                              \
            ::smartmic::test::fail(__FILE__, __LINE__,                                \
                                   "CHECK_EQ(" #a ", " #b ") -> " +                   \
                                       std::to_string(static_cast<long long>(_a)) +   \
                                       " vs " +                                       \
                                       std::to_string(static_cast<long long>(_b)));   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                         \
    do {                                                                              \
        const double _a = static_cast<double>(a);                                     \
        const double _b = static_cast<double>(b);                                     \
        if (std::fabs(_a - _b) > (tol))                                               \
            ::smartmic::test::fail(__FILE__, __LINE__,                                \
                                   "CHECK_NEAR(" #a ", " #b ") -> " +                 \
                                       std::to_string(_a) + " vs " +                  \
                                       std::to_string(_b));                           \
    } while (0)
