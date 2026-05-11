#pragma once

#include "../ble/scanner.h"
#include "../config.h"

#include <cstdint>

namespace switchbot {
namespace history {

struct HistoryServiceOptions {
    std::uint32_t newSensorWindowSeconds = 6U * 60U * 60U;
    std::uint32_t commandTimeoutMs = 3000;
    std::uint32_t sampleIntervalSeconds = 15U * 60U;
    std::uint32_t historyLimitSeconds = 68U * 24U * 60U * 60U;
    // TODO: bulk upload is not enabled yet. Start with 100 readings per request,
    // then test increasing toward the backend max of 1000.
    std::uint32_t bulkBatchLimit = 100;
};

struct HistoryServiceState {
    bool startupSyncDone = false;
};

#ifdef ARDUINO
void maybeRunStartupHistorySync(const Config& config,
                                ble::Scanner& scanner,
                                bool hasValidTime,
                                HistoryServiceState& state,
                                const HistoryServiceOptions& options = HistoryServiceOptions{});
#endif

}  // namespace history
}  // namespace switchbot
