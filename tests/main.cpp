#include <exception>
#include <iostream>
#include <string>

void runConfigTests();
void runStateTests();
void runServiceTests();
void runTimingTests();
void runUiStateTests();

int main() {
    int failures = 0;

    auto runSuite = [&](const std::string& name, void (*fn)()) {
        try {
            fn();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
        }
    };

    runSuite("config", runConfigTests);
    runSuite("state", runStateTests);
    runSuite("service", runServiceTests);
    runSuite("timing", runTimingTests);
    runSuite("ui state", runUiStateTests);

    if (failures != 0) {
        std::cerr << failures << " test suite(s) failed\n";
        return 1;
    }

    std::cout << "All test suites passed\n";
    return 0;
}
