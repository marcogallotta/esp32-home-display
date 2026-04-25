#ifndef ARDUINO

#include "config.h"
#include "platform.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>

namespace platform {

bool initTime(const Config& config, unsigned long timeoutMs) {
    (void)config;
    (void)timeoutMs;
    // No initialization needed on desktop
    return true;
}

bool hasValidTime() {
    return true;
}

std::uint64_t millis() {
    static const auto start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count()
    );
}

void delayMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void printLine(const std::string& s) {
    std::cout << s << std::endl;
}

} // namespace platform
#endif
