#pragma once

#include "../ble/scanner.h"
#include "../config.h"

#include <cstdint>

namespace switchbot {
namespace history {

struct HistoryServiceOptions {
    std::uint32_t startupWindowSeconds = 6U * 60U * 60U;
    std::uint32_t commandTimeoutMs = 3000;
    std::uint32_t delayBetweenSensorsMs = 500;
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
