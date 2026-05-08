#include "history_service.h"

#ifdef ARDUINO
#include "history_backend.h"

#include "../log.h"

#include <cstdint>
#include <ctime>
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
};

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
    out.startupWindowSeconds = config.switchbot.history.newSensorWindowSeconds;
    out.sampleIntervalSeconds = config.switchbot.history.sampleIntervalSeconds;
    out.historyLimitSeconds = config.switchbot.history.historyLimitSeconds;
    out.bulkBatchLimit = config.switchbot.history.bulkBatchLimit;
    return out;
}

HistoryPlanningOptions planningOptions(const HistoryServiceOptions& options) {
    HistoryPlanningOptions out;
    out.sampleIntervalSeconds = options.sampleIntervalSeconds;
    out.newSensorWindowSeconds = options.startupWindowSeconds;
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
        LogLevel::Info,
        "SwitchBot history: " + label + " " + windowAction(window.source) + " " +
        std::to_string(window.pointCount) + " target " + formatDuration(options.sampleIntervalSeconds) +
        " readings from " + formatIsoUtc(window.startEpoch) + " to " +
        formatIsoUtc(window.endEpoch)
    );

    if (window.pointCount > 0) {
        logLine(
            LogLevel::Debug,
            "SwitchBot history window detail: " + label +
            " source=" + window.source +
            " first_point=" + formatIsoUtc(window.firstPointEpoch) +
            " last_point=" + formatIsoUtc(window.lastPointEpoch) +
            " clamped_68d=" + yesNo(window.clampedToHistoryLimit) +
            " mac=" + sensor.mac +
            " sensor_id=" + sensor.sensorId
        );
    }
}

PlanTotals logSensorPlan(const BackendSensorInfo& sensor,
                         const std::map<std::string, std::string>& labelsByMac,
                         std::uint32_t nowEpoch,
                         const HistoryServiceOptions& options) {
    PlanTotals totals;
    totals.sensors = 1;

    const std::string label = labelForMac(labelsByMac, sensor.mac);
    const auto windows = planHistoryWindows(sensor, nowEpoch, planningOptions(options));

    if (sensor.syncIntervalsCapped) {
        totals.cappedSensors = 1;
        logLine(
            LogLevel::Warn,
            "SwitchBot history: backend capped missing intervals for " + label +
            "; upload these windows and look up again later"
        );
    }

    for (const PlannedHistoryWindow& window : windows) {
        ++totals.windows;
        totals.plannedPoints += window.pointCount;
    }
    if (!windows.empty()) {
        totals.sensorsWithWindows = 1;
    }

    logLine(
        LogLevel::Debug,
        "SwitchBot history sensor plan: " + label +
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

void addTotals(PlanTotals& total, const PlanTotals& add) {
    total.sensors += add.sensors;
    total.sensorsWithWindows += add.sensorsWithWindows;
    total.windows += add.windows;
    total.plannedPoints += add.plannedPoints;
    total.cappedSensors += add.cappedSensors;
}

}  // namespace

void maybeRunStartupHistorySync(const Config& config,
                                ble::Scanner& scanner,
                                bool hasValidTime,
                                HistoryServiceState& state,
                                const HistoryServiceOptions& options) {
    (void)scanner;

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

    logLine(
        LogLevel::Info,
        "SwitchBot history planning started: lookup-only mode; sensors=" +
        std::to_string(macs.size()) +
        "; target interval=" + formatDuration(effective.sampleIntervalSeconds) +
        "; new sensor window=" + formatDuration(effective.startupWindowSeconds) +
        "; history limit=" + formatDuration(effective.historyLimitSeconds)
    );

    logLine(
        LogLevel::Debug,
        "SwitchBot history planning detail: upload batch limit=" +
        std::to_string(effective.bulkBatchLimit)
    );

    const SensorLookupResult lookup = postSensorLookup(config, macs);
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
    const std::uint32_t nowEpoch = static_cast<std::uint32_t>(now);
    for (const BackendSensorInfo& sensor : lookup.sensors) {
        addTotals(totals, logSensorPlan(sensor, labelsByMac, nowEpoch, effective));
    }

    logLine(
        LogLevel::Info,
        "SwitchBot history planning done: sensors=" + std::to_string(totals.sensors) +
        "; sensors_with_windows=" + std::to_string(totals.sensorsWithWindows) +
        "; windows=" + std::to_string(totals.windows) +
        "; target_readings=" + std::to_string(totals.plannedPoints) +
        "; capped_sensors=" + std::to_string(totals.cappedSensors)
    );
}

}  // namespace history
}  // namespace switchbot

#endif  // ARDUINO
