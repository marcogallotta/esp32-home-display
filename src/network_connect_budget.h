#pragma once

#include <cstdint>

namespace network {

// Tracks a WiFi connection attempt start time and computes how much of the
// timeout budget remains, so work done between kickStart() and remainingMs()
// is credited against the budget rather than wasted.
struct WifiConnectBudget {
    bool started = false;
    uint32_t startedMs = 0;

    void kickStart(uint32_t nowMs) {
        started = true;
        startedMs = nowMs;
    }

    void clear() {
        started = false;
    }

    uint32_t remainingMs(uint32_t nowMs, uint32_t timeoutMs) const {
        if (!started) return timeoutMs;
        const uint32_t elapsed = nowMs - startedMs;
        return elapsed >= timeoutMs ? 0 : timeoutMs - elapsed;
    }
};

} // namespace network
