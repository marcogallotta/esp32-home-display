#include "history_service.h"

#ifdef ARDUINO
#include "history_sync.h"
#include "../log.h"
#include "../platform.h"

#include <ctime>
#include <string>

namespace switchbot {
namespace history {
namespace {

std::string sensorLabel(const SwitchbotSensorConfig& sensor) {
    if (!sensor.shortName.empty()) {
        return sensor.shortName;
    }
    if (!sensor.name.empty()) {
        return sensor.name;
    }
    return sensor.mac;
}

SyncRequest makeRequest(std::time_t now, const HistoryServiceOptions& options) {
    SyncRequest request;
    request.commandTimeoutMs = options.commandTimeoutMs;
    request.endEpoch = static_cast<std::uint32_t>(now);
    request.startEpoch = request.endEpoch > options.startupWindowSeconds
        ? request.endEpoch - options.startupWindowSeconds
        : 0;
    return request;
}

void runOneSensor(const SwitchbotSensorConfig& sensor,
                  const HistoryServiceOptions& options,
                  std::time_t now) {
    const std::string label = sensorLabel(sensor);

    logLine(LogLevel::Info, "switchbot_history_sync_start," + label + "," + sensor.mac);

    const SyncResult result = syncSensorHistory(sensor.mac, makeRequest(now, options));
    if (!result.ok()) {
        logLine(
            LogLevel::Error,
            "switchbot_history_sync_failed," + label + "," + sensor.mac + "," +
            syncStatusName(result.status) + "," + result.message
        );
        return;
    }

    logLine(
        LogLevel::Info,
        "switchbot_history_metadata," + label + "," + sensor.mac + "," +
        std::to_string(result.metadata.startEpoch) + "," +
        std::to_string(result.metadata.endEpoch) + "," +
        std::to_string(result.metadata.endIndex) + "," +
        std::to_string(result.metadata.intervalSeconds) + "," +
        std::to_string(result.samples.size())
    );

    logLine(
        LogLevel::Info,
        "switchbot_history_sync_done," + label + "," + sensor.mac + "," +
        std::to_string(result.samples.size())
    );
}

}  // namespace

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
        logLine(LogLevel::Info, "switchbot_history_service,no_configured_sensors");
        return;
    }

    const std::time_t now = std::time(nullptr);
    if (now <= 1700000000) {
        logLine(LogLevel::Warn, "switchbot_history_service,skipped,invalid_time");
        return;
    }

    logLine(LogLevel::Info, "switchbot_history_service,start,api_submission_disabled,window_seconds=" + std::to_string(options.startupWindowSeconds));

    scanner.stop();
    for (const SwitchbotSensorConfig& sensor : config.switchbot.sensors) {
        runOneSensor(sensor, options, now);
        if (options.delayBetweenSensorsMs > 0) {
            platform::delayMs(static_cast<int>(options.delayBetweenSensorsMs));
        }
    }
    scanner.start();

    logLine(LogLevel::Info, "switchbot_history_service,done");
}

}  // namespace history
}  // namespace switchbot

#endif  // ARDUINO
