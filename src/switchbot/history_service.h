#pragma once

#include "../ble/scanner.h"
#include "../config.h"
#include "history_backend.h"
#include "history_sync.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace switchbot {
namespace history {

struct HistoryServiceOptions {
    std::uint32_t newSensorWindowSeconds = 68U * 24U * 60U * 60U;
    std::uint32_t commandTimeoutMs = 3000;
    std::uint32_t sampleIntervalSeconds = 15U * 60U;
    std::uint32_t historyLimitSeconds = 68U * 24U * 60U * 60U;
    std::uint32_t bulkBatchLimit = 1000;
};

struct HistoryServiceState {
    bool startupSyncDone = false;
};

struct HistoryServiceDeps {
    std::function<SensorLookupResult(const std::vector<std::string>&)> sensorLookup;
    std::function<std::unique_ptr<ISensorHistorySession>(const std::string&)> sessionFactory;
    std::function<BulkUploadResult(const std::string&, const std::vector<BulkHistoryReading>&)> bulkUpload;
};

void runHistorySync(const std::vector<std::string>& macs,
                    const std::map<std::string, std::string>& labelsByMac,
                    std::uint32_t nowEpoch,
                    const HistoryServiceOptions& options,
                    const HistoryServiceDeps& deps);

#ifdef ARDUINO
void maybeRunStartupHistorySync(const Config& config,
                                ble::Scanner& scanner,
                                bool hasValidTime,
                                HistoryServiceState& state,
                                const HistoryServiceOptions& options = HistoryServiceOptions{});
#endif

}  // namespace history
}  // namespace switchbot
