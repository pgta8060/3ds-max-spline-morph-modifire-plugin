#include "Test.h"

#include <chrono>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) tw_test::outputDir() = argv[++i];
        else filter = argv[i];
    }
    std::filesystem::create_directories(tw_test::outputDir());

    int run = 0, failedTests = 0;
    for (const auto& t : tw_test::registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = tw_test::failures();
        const auto start = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool ok = tw_test::failures() == before;
        std::printf("[ %s ] %s (%.1f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++run;
        if (!ok) ++failedTests;
    }
    std::printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n", run, tw_test::checks(),
                failedTests, tw_test::failures());
    return failedTests == 0 ? 0 : 1;
}
