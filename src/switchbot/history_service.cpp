#include "history_service.h"

#include "history_backend.h"
#include "history_sync.h"

#include "../log.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace switchbot {
namespace history {
namespace {

struct PlanTotals {
    std::uint32_t sensors = 0;
    std::uint32_t sensorsWithWindows = 0;
    std::uint32_t windows = 0;
    std::uint32_t plannedPoints = 0;
    std::uint32_t cappedSensors = 0;
    std::uint32_t syncedWindows = 0;
    std::uint32_t syncFailures = 0;
    std::uint32_t selectedReadings = 0;
    std::uint32_t uploadedReadings = 0;
    std::uint32_t uploadFailures = 0;
    std::uint32_t uploadRowErrors = 0;
};

void addTotals(PlanTotals& total, const PlanTotals& add);

std::string sensorLabel(const SwitchbotSensorConfig& sensor) {
    if (!sensor.name.empty()) {
        return sensor.name;
    }
    if (!sensor.shortName.empty()) {
        return sensor.shortName;
    }
    return sensor.mac;
}

HistoryServiceOptions effectiveOptions(const Config& config, const HistoryServiceOptions& defaults) {
    HistoryServiceOptions out = defaults;
    out.newSensorWindowSeconds = config.switchbot.history.newSensorWindowSeconds;
    out.sampleIntervalSeconds = config.switchbot.history.sampleIntervalSeconds;
    out.historyLimitSeconds = config.switchbot.history.historyLimitSeconds;
    out.bulkBatchLimit = config.switchbot.history.bulkBatchLimit;
    return out;
}

HistoryPlanningOptions planningOptions(const HistoryServiceOptions& options) {
    HistoryPlanningOptions out;
    out.sampleIntervalSeconds = options.sampleIntervalSeconds;
    out.newSensorWindowSeconds = options.newSensorWindowSeconds;
    out.historyLimitSeconds = options.historyLimitSeconds;
    return out;
}

std::string formatDuration(std::uint32_t seconds) {
    if (seconds == 0) {
        return "0s";
    }
    if (seconds % 86400 == 0) {
        return std::to_string(seconds / 86400) + "d";
    }
    if (seconds % 3600 == 0) {
        return std::to_string(seconds / 3600) + "h";
    }
    if (seconds % 60 == 0) {
        return std::to_string(seconds / 60) + "m";
    }
    return std::to_string(seconds) + "s";
}

std::vector<std::string> configuredMacs(const Config& config,
                                        std::map<std::string, std::string>& labelsByMac) {
    std::vector<std::string> macs;
    std::set<std::string> seen;

    for (const SwitchbotSensorConfig& sensor : config.switchbot.sensors) {
        const std::string mac = normalizeMac(sensor.mac);
        if (mac.empty()) {
            logLine(LogLevel::Warn, "SwitchBot history: ignoring configured sensor with invalid MAC " + sensor.mac);
            continue;
        }
        labelsByMac[mac] = sensorLabel(sensor);
        if (seen.insert(mac).second) {
            macs.push_back(mac);
        }
    }

    return macs;
}

std::string labelForMac(const std::map<std::string, std::string>& labelsByMac, const std::string& mac) {
    const auto it = labelsByMac.find(mac);
    if (it == labelsByMac.end() || it->second.empty()) {
        return mac;
    }
    return it->second;
}

std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

std::string nullableTimestamp(const std::optional<std::string>& value) {
    return value.has_value() ? *value : "none";
}

void logLookupWarnings(const SensorLookupResult& result) {
    for (const std::string& warning : result.warnings) {
        logLine(LogLevel::Warn, "SwitchBot history backend warning: " + warning);
    }
}

void logMissingConfiguredSensors(const std::vector<std::string>& requestedMacs,
                                 const SensorLookupResult& result,
                                 const std::map<std::string, std::string>& labelsByMac) {
    std::set<std::string> returned;
    for (const BackendSensorInfo& sensor : result.sensors) {
        returned.insert(sensor.mac);
    }

    for (const std::string& mac : requestedMacs) {
        if (returned.find(mac) == returned.end()) {
            logLine(
                LogLevel::Warn,
                "SwitchBot history: backend did not return configured sensor " +
                labelForMac(labelsByMac, mac) + " (" + mac + ")"
            );
        }
    }
}

std::string windowAction(const std::string& source) {
    if (source == "leading_backfill") {
        return "older backend backfill needs";
    }
    if (source == "internal_gap") {
        return "backend internal gap needs";
    }
    if (source == "trailing") {
        return "trailing catch-up needs";
    }
    if (source == "new_sensor") {
        return "new sensor backfill needs";
    }
    return source + " needs";
}

void logPlannedWindow(const std::string& label,
                      const BackendSensorInfo& sensor,
                      const PlannedHistoryWindow& window,
                      const HistoryServiceOptions& options) {
    logLine(
        LogLevel::Trace,
        "SwitchBot history planned window: " + label +
        " source=" + window.source +
        " action=" + windowAction(window.source) +
        " target_readings=" + std::to_string(window.pointCount) +
        " interval=" + formatDuration(options.sampleIntervalSeconds) +
        " from=" + formatIsoUtc(window.startEpoch) +
        " to=" + formatIsoUtc(window.endEpoch)
    );

    if (window.pointCount > 0) {
        logLine(
            LogLevel::Trace,
            "SwitchBot history window detail: " + label +
            " source=" + window.source +
            " first_point=" + formatIsoUtc(window.firstPointEpoch) +
            " last_point=" + formatIsoUtc(window.lastPointEpoch) +
            " clamped_history_limit=" + yesNo(window.clampedToHistoryLimit) +
            " mac=" + sensor.mac +
            " sensor_id=" + sensor.sensorId
        );
    }
}

PlanTotals logSensorPlan(const BackendSensorInfo& sensor,
                         const std::map<std::string, std::string>& labelsByMac,
                         const std::vector<PlannedHistoryWindow>& windows,
                         const HistoryServiceOptions& options) {
    PlanTotals totals;
    totals.sensors = 1;

    const std::string label = labelForMac(labelsByMac, sensor.mac);

    if (sensor.syncIntervalsCapped) {
        totals.cappedSensors = 1;
        logLine(
            LogLevel::Warn,
            "SwitchBot history: backend capped missing intervals for " + label +
            "; upload these windows and look up again later"
        );
    }

    std::uint32_t leadingWindows = 0;
    std::uint32_t internalGapWindows = 0;
    std::uint32_t trailingWindows = 0;
    std::uint32_t newSensorWindows = 0;

    for (const PlannedHistoryWindow& window : windows) {
        ++totals.windows;
        totals.plannedPoints += window.pointCount;
        if (window.source == "leading_backfill") {
            ++leadingWindows;
        } else if (window.source == "internal_gap") {
            ++internalGapWindows;
        } else if (window.source == "trailing") {
            ++trailingWindows;
        } else if (window.source == "new_sensor") {
            ++newSensorWindows;
        }
    }
    if (!windows.empty()) {
        totals.sensorsWithWindows = 1;
    }

    logLine(
        LogLevel::Info,
        "SwitchBot history sensor plan: " + label +
        " windows=" + std::to_string(windows.size()) +
        " target_readings=" + std::to_string(totals.plannedPoints) +
        " leading=" + std::to_string(leadingWindows) +
        " gaps=" + std::to_string(internalGapWindows) +
        " trailing=" + std::to_string(trailingWindows) +
        " new_sensor=" + std::to_string(newSensorWindows) +
        " capped=" + yesNo(sensor.syncIntervalsCapped)
    );

    logLine(
        LogLevel::Debug,
        "SwitchBot history sensor backend detail: " + label +
        " stored_first=" + nullableTimestamp(sensor.firstTimestamp) +
        " stored_latest=" + nullableTimestamp(sensor.latestTimestamp) +
        " internal_gaps=" + std::to_string(sensor.syncIntervals.size()) +
        " planned_windows=" + std::to_string(windows.size()) +
        " target_readings=" + std::to_string(totals.plannedPoints)
    );

    logLine(
        LogLevel::Debug,
        "SwitchBot history sensor detail: " + label +
        " mac=" + sensor.mac +
        " sensor_id=" + sensor.sensorId +
        " capped=" + yesNo(sensor.syncIntervalsCapped)
    );

    for (const PlannedHistoryWindow& window : windows) {
        logPlannedWindow(label, sensor, window, options);
    }

    return totals;
}

std::uint32_t expandedStartEpoch(const PlannedHistoryWindow& window, std::uint32_t deviceIntervalSeconds) {
    if (deviceIntervalSeconds == 0) {
        return window.startEpoch;
    }
    if (window.startEpoch <= deviceIntervalSeconds) {
        return 0;
    }
    return window.startEpoch - deviceIntervalSeconds;
}

std::uint32_t expandedEndEpoch(const PlannedHistoryWindow& window, std::uint32_t deviceIntervalSeconds) {
    if (deviceIntervalSeconds == 0) {
        return window.endEpoch;
    }
    if (window.endEpoch > std::numeric_limits<std::uint32_t>::max() - deviceIntervalSeconds) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return window.endEpoch + deviceIntervalSeconds;
}

std::uint32_t uploadBatchLimit(const HistoryServiceOptions& options) {
    if (options.bulkBatchLimit == 0) {
        return 100;
    }
    return std::min<std::uint32_t>(options.bulkBatchLimit, 1000U);
}

void logBulkErrors(const std::string& label, const BulkUploadResult& result) {
    for (const BulkUploadError& error : result.errors) {
        logLine(
            LogLevel::Warn,
            "SwitchBot history upload row error: " + label +
            " index=" + std::to_string(error.index) +
            " code=" + error.code +
            " message=" + error.message
        );
    }
}

PlanTotals uploadReadings(const HistoryServiceDeps& deps,
                          const std::string& label,
                          const std::string& sensorId,
                          const std::vector<BulkHistoryReading>& readings,
                          const HistoryServiceOptions& options) {
    PlanTotals totals;
    const std::uint32_t limit = uploadBatchLimit(options);

    for (std::size_t offset = 0; offset < readings.size(); offset += limit) {
        const std::size_t end = std::min<std::size_t>(readings.size(), offset + limit);
        std::vector<BulkHistoryReading> batch(readings.begin() + offset, readings.begin() + end);

        const BulkUploadResult upload = deps.bulkUpload(sensorId, batch);
        if (!upload.ok) {
            ++totals.uploadFailures;
            logLine(
                LogLevel::Error,
                "SwitchBot history upload failed: " + label +
                " http=" + std::to_string(upload.httpStatusCode) +
                "; " + upload.error
            );
            continue;
        }

        totals.uploadedReadings += static_cast<std::uint32_t>(batch.size());
        totals.uploadRowErrors += static_cast<std::uint32_t>(upload.errors.size());
        logLine(
            LogLevel::Info,
            "SwitchBot history upload complete: " + label +
            " readings=" + std::to_string(batch.size()) +
            " row_errors=" + std::to_string(upload.errors.size())
        );
        logBulkErrors(label, upload);
    }

    return totals;
}

std::string windowResultReason(const PlannedHistoryWindow& window,
                               const SyncResult& sync,
                               const std::vector<BulkHistoryReading>& selected) {
    if (window.pointCount == 0) {
        return "no_targets";
    }
    if (!selected.empty()) {
        return selected.size() >= window.pointCount ? "selected_all" : "selected_partial";
    }
    if (sync.metadata.intervalSeconds == 0 || sync.metadata.endIndex == 0) {
        return "empty_metadata";
    }

    const std::uint32_t metadataEndEpoch =
        sync.metadata.startEpoch + sync.metadata.endIndex * sync.metadata.intervalSeconds;
    if (window.lastPointEpoch < sync.metadata.startEpoch) {
        return "before_metadata";
    }
    if (window.firstPointEpoch >= metadataEndEpoch) {
        return "after_metadata";
    }
    if (sync.samples.empty()) {
        return "no_raw_overlap";
    }
    return "raw_without_target_match";
}

void logWindowResult(const std::string& label,
                     const PlannedHistoryWindow& window,
                     const SyncResult& sync,
                     const std::vector<BulkHistoryReading>& selected) {
    const std::uint32_t metadataEndEpoch = sync.metadata.intervalSeconds == 0
        ? sync.metadata.startEpoch
        : sync.metadata.startEpoch + sync.metadata.endIndex * sync.metadata.intervalSeconds;
    logLine(
        LogLevel::Debug,
        "switchbot_history_window_result," + label +
        ",source=" + window.source +
        ",target=" + std::to_string(window.pointCount) +
        ",raw=" + std::to_string(sync.samples.size()) +
        ",selected=" + std::to_string(selected.size()) +
        ",missing=" + std::to_string(window.pointCount > selected.size() ? window.pointCount - selected.size() : 0) +
        ",reason=" + windowResultReason(window, sync, selected) +
        ",window=" + formatIsoUtc(window.firstPointEpoch) + ".." + formatIsoUtc(window.lastPointEpoch) +
        ",metadata=" + formatIsoUtc(sync.metadata.startEpoch) + ".." + formatIsoUtc(metadataEndEpoch) +
        ",end_index=" + std::to_string(sync.metadata.endIndex) +
        ",interval=" + std::to_string(sync.metadata.intervalSeconds) + "s"
    );
}

PlanTotals syncAndUploadWindow(const HistoryServiceDeps& deps,
                               const std::string& label,
                               const BackendSensorInfo& sensor,
                               const PlannedHistoryWindow& window,
                               std::uint32_t nowEpoch,
                               const HistoryServiceOptions& options,
                               ISensorHistorySession& session) {
    PlanTotals totals;

    SyncRequest request;
    request.startEpoch = expandedStartEpoch(window, 60);
    request.endEpoch = expandedEndEpoch(window, 60);
    request.timeSyncEpoch = nowEpoch;
    request.commandTimeoutMs = options.commandTimeoutMs;
    request.progressLabel = label + " " + window.source;

    logLine(
        LogLevel::Trace,
        "SwitchBot history sync window: " + label +
        " source=" + window.source +
        " targets=" + std::to_string(window.pointCount) +
        " from=" + formatIsoUtc(window.firstPointEpoch) +
        " to=" + formatIsoUtc(window.lastPointEpoch)
    );

    const SyncResult sync = session.fetch(request);

    const std::uint32_t deviceInterval = sync.metadata.intervalSeconds == 0 ? 60U : sync.metadata.intervalSeconds;
    std::vector<BulkHistoryReading> selected = selectAlignedReadings(
        sync.samples,
        window,
        options.sampleIntervalSeconds,
        deviceInterval
    );
    totals.selectedReadings += static_cast<std::uint32_t>(selected.size());

    if (!sync.ok()) {
        ++totals.syncFailures;
        if (!selected.empty()) {
            addTotals(totals, uploadReadings(deps, label, sensor.sensorId, selected, options));
        }
        logLine(
            LogLevel::Error,
            "SwitchBot history sync failed: " + label +
            " source=" + window.source +
            " status=" + syncStatusName(sync.status) +
            "; " + sync.message
        );
        return totals;
    }

    ++totals.syncedWindows;
    logWindowResult(label, window, sync, selected);

    if (selected.empty()) {
        return totals;
    }

    addTotals(totals, uploadReadings(deps, label, sensor.sensorId, selected, options));
    return totals;
}

PlanTotals syncAndUploadSensor(const HistoryServiceDeps& deps,
                               const BackendSensorInfo& sensor,
                               const std::map<std::string, std::string>& labelsByMac,
                               const std::vector<PlannedHistoryWindow>& windows,
                               std::uint32_t nowEpoch,
                               const HistoryServiceOptions& options) {
    PlanTotals totals;
    const std::string label = labelForMac(labelsByMac, sensor.mac);

    if (windows.empty()) {
        return totals;
    }

    auto session = deps.sessionFactory(sensor.mac);
    const SyncResult openResult = session->open();
    if (!openResult.ok()) {
        logLine(
            LogLevel::Error,
            "SwitchBot history connect failed: " + label +
            " status=" + syncStatusName(openResult.status) +
            "; " + openResult.message
        );
        totals.syncFailures = static_cast<std::uint32_t>(windows.size());
        return totals;
    }

    for (const PlannedHistoryWindow& window : windows) {
        addTotals(totals, syncAndUploadWindow(deps, label, sensor, window, nowEpoch, options, *session));
    }

    return totals;
}

void addTotals(PlanTotals& total, const PlanTotals& add) {
    total.sensors += add.sensors;
    total.sensorsWithWindows += add.sensorsWithWindows;
    total.windows += add.windows;
    total.plannedPoints += add.plannedPoints;
    total.cappedSensors += add.cappedSensors;
    total.syncedWindows += add.syncedWindows;
    total.syncFailures += add.syncFailures;
    total.selectedReadings += add.selectedReadings;
    total.uploadedReadings += add.uploadedReadings;
    total.uploadFailures += add.uploadFailures;
    total.uploadRowErrors += add.uploadRowErrors;
}

}  // namespace

void runHistorySync(const std::vector<std::string>& macs,
                    const std::map<std::string, std::string>& labelsByMac,
                    std::uint32_t nowEpoch,
                    const HistoryServiceOptions& options,
                    const HistoryServiceDeps& deps) {
    logLine(
        LogLevel::Info,
        "SwitchBot history sync started: sensors=" +
        std::to_string(macs.size()) +
        "; target interval=" + formatDuration(options.sampleIntervalSeconds) +
        "; new sensor window=" + formatDuration(options.newSensorWindowSeconds) +
        "; history limit=" + formatDuration(options.historyLimitSeconds)
    );

    logLine(
        LogLevel::Debug,
        "SwitchBot history planning detail: upload batch limit=" +
        std::to_string(options.bulkBatchLimit)
    );

    const SensorLookupResult lookup = deps.sensorLookup(macs);
    if (!lookup.ok) {
        logLine(
            LogLevel::Error,
            "SwitchBot history lookup failed: http=" + std::to_string(lookup.httpStatusCode) +
            "; " + lookup.error
        );
        return;
    }

    logLine(
        LogLevel::Info,
        "SwitchBot history lookup complete: backend returned " +
        std::to_string(lookup.sensors.size()) + " sensors, warnings=" +
        std::to_string(lookup.warnings.size())
    );
    logLookupWarnings(lookup);
    logMissingConfiguredSensors(macs, lookup, labelsByMac);

    PlanTotals totals;
    const HistoryPlanningOptions planning = planningOptions(options);

    std::vector<std::vector<PlannedHistoryWindow>> allWindows;
    allWindows.reserve(lookup.sensors.size());
    for (const BackendSensorInfo& sensor : lookup.sensors) {
        allWindows.push_back(planHistoryWindows(sensor, nowEpoch, planning));
    }

    for (std::size_t i = 0; i < lookup.sensors.size(); ++i) {
        addTotals(totals, logSensorPlan(lookup.sensors[i], labelsByMac, allWindows[i], options));
    }

    logLine(
        LogLevel::Info,
        "SwitchBot history planning done: sensors=" + std::to_string(totals.sensors) +
        "; sensors_with_windows=" + std::to_string(totals.sensorsWithWindows) +
        "; windows=" + std::to_string(totals.windows) +
        "; target_readings=" + std::to_string(totals.plannedPoints) +
        "; capped_sensors=" + std::to_string(totals.cappedSensors)
    );

    if (totals.windows == 0) {
        return;
    }

    for (std::size_t i = 0; i < lookup.sensors.size(); ++i) {
        addTotals(totals, syncAndUploadSensor(deps, lookup.sensors[i], labelsByMac, allWindows[i], nowEpoch, options));
    }

    logLine(
        LogLevel::Info,
        "SwitchBot history sync done: windows=" + std::to_string(totals.syncedWindows) +
        "; sync_failures=" + std::to_string(totals.syncFailures) +
        "; selected_readings=" + std::to_string(totals.selectedReadings) +
        "; uploaded_readings=" + std::to_string(totals.uploadedReadings) +
        "; upload_failures=" + std::to_string(totals.uploadFailures) +
        "; upload_row_errors=" + std::to_string(totals.uploadRowErrors)
    );
}

#ifdef ARDUINO
#include "history_backend.h"
#include <Arduino.h>

void maybeRunStartupHistorySync(const Config& config,
                                ble::Scanner& scanner,
                                bool hasValidTime,
                                HistoryServiceState& state,
                                const HistoryServiceOptions& options) {
    if (state.startupSyncDone || !hasValidTime) {
        return;
    }

    state.startupSyncDone = true;

    if (config.switchbot.sensors.empty()) {
        logLine(LogLevel::Info, "SwitchBot history: no configured sensors");
        return;
    }

    const std::time_t now = std::time(nullptr);
    if (now <= 1700000000) {
        logLine(LogLevel::Warn, "SwitchBot history: skipped because device time is not valid yet");
        return;
    }

    const HistoryServiceOptions effective = effectiveOptions(config, options);

    std::map<std::string, std::string> labelsByMac;
    const std::vector<std::string> macs = configuredMacs(config, labelsByMac);
    if (macs.empty()) {
        logLine(LogLevel::Warn, "SwitchBot history: skipped because no configured sensors had valid MACs");
        return;
    }

    HistoryServiceDeps deps;
    deps.sensorLookup = [&](const std::vector<std::string>& m) {
        return postSensorLookup(config, m);
    };
    deps.sessionFactory = [](const std::string& mac) {
        return std::make_unique<SensorHistorySession>(mac);
    };
    deps.bulkUpload = [&](const std::string& sensorId, const std::vector<BulkHistoryReading>& readings) {
        return postBulkUpload(config, sensorId, readings);
    };

    const std::uint32_t nowEpoch = static_cast<std::uint32_t>(now);

    scanner.stop();
    runHistorySync(macs, labelsByMac, nowEpoch, effective, deps);
    scanner.start();
}
#endif  // ARDUINO

}  // namespace history
}  // namespace switchbot
