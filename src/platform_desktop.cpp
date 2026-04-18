#ifndef ARDUINO

#include "config.h"
#include "platform.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>

namespace platform {

bool initTime(const Config& config) {
    // No initialization needed on desktop
    return true;
}

void delayMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void printLine(const std::string& s) {
    std::cout << s << std::endl;
}

} // namespace platform
#endif
