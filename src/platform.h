#pragma once

#include <string>

#include "config.h"

namespace platform {

// Initialize time subsystem (e.g. sync with NTP server on embedded platforms)
bool initTime(const Config& Config);

// Sleep/delay in milliseconds
void delayMs(int ms);

// Print line (for now console)
void printLine(const std::string& s);

} // namespace platform
