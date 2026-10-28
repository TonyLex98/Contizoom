#include "test.h"

#include <cstring>

// Runs all registered tests, or only those whose name contains argv[1].
int main(int argc, char* argv[]) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (const auto& c : ic_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = ic_test::failures();
        try {
            c.fn();
        } catch (const std::exception& e) {
            ic_test::fail(c.name, 0, std::string("unexpected exception: ") + e.what());
        }
        ++run;
        if (ic_test::failures() != before) {
            ++failedCases;
            std::cerr << "FAIL " << c.name << "\n";
        }
    }
    std::cout << run - failedCases << "/" << run << " tests passed\n";
    return failedCases == 0 ? 0 : 1;
}
