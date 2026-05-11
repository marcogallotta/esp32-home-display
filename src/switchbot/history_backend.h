#pragma once

#include "../config.h"
#include "history_protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace switchbot {
namespace history {

struct BackendSyncInterval {
    std::uint32_t startEpoch = 0;
    std::uint32_t endEpoch = 0;
};

struct BackendSensorInfo {
    std::string mac;
    std::string sensorId;
    std::optional<std::string> firstTimestamp;
    std::optional<std::string> latestTimestamp;
    std::optional<std::uint32_t> firstEpoch;
    std::optional<std::uint32_t> latestEpoch;
    std::vector<BackendSyncInterval> syncIntervals;
    bool syncIntervalsCapped = false;
};

struct SensorLookupResult {
    bool ok = false;
    int httpStatusCode = 0;
    std::string error;
    std::vector<BackendSensorInfo> sensors;
    std::vector<std::string> warnings;
};

struct BulkHistoryReading {
    std::uint32_t timestampEpoch = 0;
    double temperatureC = 0.0;
    std::uint8_t humidityPct = 0;
};

struct BulkUploadError {
    std::uint32_t index = 0;
    std::string code;
    std::string message;
};

struct BulkUploadResult {
    bool ok = false;
    int httpStatusCode = 0;
    std::string error;
    std::vector<BulkUploadError> errors;
};

struct PlannedHistoryWindow {
    std::string source;
    std::uint32_t startEpoch = 0;
    std::uint32_t endEpoch = 0;
    bool clampedToHistoryLimit = false;

    std::uint32_t firstPointEpoch = 0;
    std::uint32_t lastPointEpoch = 0;
    std::uint32_t pointCount = 0;
};

struct HistoryPlanningOptions {
    std::uint32_t sampleIntervalSeconds = 15U * 60U;
    std::uint32_t newSensorWindowSeconds = 6U * 60U * 60U;
    std::uint32_t historyLimitSeconds = 68U * 24U * 60U * 60U;
};

std::string normalizeMac(const std::string& mac);
std::optional<std::uint32_t> parseIsoUtcEpoch(const std::string& timestamp);
std::string formatIsoUtc(std::uint32_t epoch);

std::string apiUrl(const Config& config, const char* path);

std::string makeSensorLookupPayload(const std::vector<std::string>& normalizedMacs);
SensorLookupResult parseSensorLookupResponse(const std::string& body, int httpStatusCode = 200);
SensorLookupResult postSensorLookup(const Config& config, const std::vector<std::string>& normalizedMacs);

std::string makeBulkUploadPayload(const std::string& sensorId, const std::vector<BulkHistoryReading>& readings);
BulkUploadResult parseBulkUploadResponse(const std::string& body, int httpStatusCode = 200);
BulkUploadResult postBulkUpload(const Config& config, const std::string& sensorId, const std::vector<BulkHistoryReading>& readings);

std::vector<PlannedHistoryWindow> planHistoryWindows(const BackendSensorInfo& sensor,
                                                     std::uint32_t nowEpoch,
                                                     const HistoryPlanningOptions& options);

std::vector<BulkHistoryReading> selectAlignedReadings(const std::vector<Sample>& samples,
                                                      const PlannedHistoryWindow& window,
                                                      std::uint32_t sampleIntervalSeconds,
                                                      std::uint32_t deviceIntervalSeconds);

}  // namespace history
}  // namespace switchbot
