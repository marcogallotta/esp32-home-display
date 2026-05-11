#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "config.h"

namespace platform {

struct HeapStats {
    std::size_t freeBytes = 0;
    std::size_t largestFreeBlock = 0;
};

HeapStats heapStats();

// Initialize time subsystem (e.g. sync with NTP server on embedded platforms)
bool initTime(const Config& config, unsigned long timeoutMs = 15000);

// Whether local wall-clock time is currently valid
bool hasValidTime();

// Monotonic milliseconds since boot/process start.
std::uint64_t millis();

// Sleep/delay in milliseconds
void delayMs(int ms);

// Print line (for now console)
void printLine(const std::string& s);

} // namespace platform
