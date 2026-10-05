// Minimal self-registering test framework (no external dependency).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tw_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

// Directory for images / OBJ files written by tests (created by main).
inline std::string& outputDir() {
    static std::string d = "test_output";
    return d;
}

}  // namespace tw_test

#define TW_CONCAT2(a, b) a##b
#define TW_CONCAT(a, b) TW_CONCAT2(a, b)

#define TEST(name)                                                                \
    static void name();                                                           \
    static tw_test::Registrar TW_CONCAT(reg_, name)(#name, name);                 \
    static void name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++tw_test::checks();                                                              \
        if (!(cond)) {                                                                    \
            ++tw_test::failures();                                                        \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                             \
    do {                                                                                  \
        ++tw_test::checks();                                                              \
        const double tw_a = (a), tw_b = (b);                                              \
        if (!(std::fabs(tw_a - tw_b) <= (tol))) {                                         \
            ++tw_test::failures();                                                        \
            std::printf("    FAILED %s:%d: %s = %.6g, expected %.6g (tol %.3g)\n", __FILE__, \
                        __LINE__, #a, tw_a, tw_b, static_cast<double>(tol));              \
        }                                                                                 \
    } while (0)

#define REQUIRE(cond)                                                                     \
    do {                                                                                  \
        ++tw_test::checks();                                                              \
        if (!(cond)) {                                                                    \
            ++tw_test::failures();                                                        \
            std::printf("    FAILED (fatal) %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            return;                                                                       \
        }                                                                                 \
    } while (0)
