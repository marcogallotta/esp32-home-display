#include "api_sync.h"

#include <cstdint>
#include <ctime>
#include <string>

#include "log.h"

namespace {
constexpr std::int64_t kXiaomiBufferWindowSeconds = 60;

bool isAccepted(api::BackendWriteResult result) {
    return result == api::BackendWriteResult::Created ||
           result == api::BackendWriteResult::Duplicate ||
           result == api::BackendWriteResult::Merged;
}

bool hasValidApiTimestamp(const std::optional<std::int64_t>& timestamp) {
    return timestamp.has_value() && *timestamp > 0;
}

void logSkippedInvalidTimestamp(
    const char* sensorType,
    const SensorIdentity& identity
) {
    logLine(
        LogLevel::Debug,
        std::string("Skipping API write for ") + sensorType + " " + identity.name +
        ": sensor timestamp is not available yet"
    );
}

const char* writeStatusName(api::WriteStatus status) {
    switch (status) {
        case api::WriteStatus::Sent:
            return "sent";
        case api::WriteStatus::Buffered:
            return "buffered";
        case api::WriteStatus::DroppedPermanent:
            return "dropped permanently";
        case api::WriteStatus::DroppedBufferFull:
            return "dropped because buffer is full";
    }

    return "unknown";
}

const char* backendWriteResultName(api::BackendWriteResult result) {
    switch (result) {
        case api::BackendWriteResult::Failed:
            return "failed";
        case api::BackendWriteResult::Created:
            return "created";
        case api::BackendWriteResult::Duplicate:
            return "duplicate";
        case api::BackendWriteResult::Merged:
            return "merged";
        case api::BackendWriteResult::Conflict:
            return "conflict";
    }

    return "unknown";
}

bool shouldLogApiWriteResult(const api::WriteResult& response) {
    if (response.status == api::WriteStatus::Buffered) {
        return false;
    }

    return true;
}

void logApiWriteResult(
    const char* sensorType,
    const SensorIdentity& identity,
    const api::WriteResult& response
) {
    if (!shouldLogApiWriteResult(response)) {
        return;
    }

    const LogLevel level =
        response.status == api::WriteStatus::DroppedPermanent ||
        response.status == api::WriteStatus::DroppedBufferFull ||
        response.backendResult == api::BackendWriteResult::Conflict ||
        response.backendResult == api::BackendWriteResult::Failed
            ? LogLevel::Warn
            : LogLevel::Info;

    logLine(
        level,
        std::string("API write result for ") +
        sensorType +
        " " + identity.name +
        ": " + writeStatusName(response.status) +
        ", backend " + backendWriteResultName(response.backendResult) +
        ", HTTP " + std::to_string(response.httpStatusCode)
    );
}

void logConflict(
    const char* sensorType,
    const SensorIdentity& identity,
    const api::WriteResult& response
) {
    logLine(
        LogLevel::Warn,
        std::string("API conflict for ") +
        sensorType +
        " " + identity.name +
        " (" + identity.mac + ")" +
        ": HTTP " + std::to_string(response.httpStatusCode) +
        ", body: " + response.body
    );
}

bool hasCompleteXiaomiReading(const XiaomiReading& reading) {
    return reading.temperatureC.has_value() &&
           reading.moisturePct.has_value() &&
           reading.lux.has_value() &&
           reading.conductivityUsCm.has_value();
}

bool differsFromLastSent(const XiaomiReading& current, const XiaomiReading& lastSent) {
    return api::shouldSendXiaomiTemperature(current, lastSent) ||
           api::shouldSendXiaomiMoisture(current, lastSent) ||
           api::shouldSendXiaomiLux(current, lastSent) ||
           api::shouldSendXiaomiConductivity(current, lastSent);
}

bool sameBufferedData(const XiaomiReading& a, const XiaomiReading& b) {
    return a.temperatureC == b.temperatureC &&
           a.moisturePct == b.moisturePct &&
           a.lux == b.lux &&
           a.conductivityUsCm == b.conductivityUsCm;
}

void resetPending(api::XiaomiBufferedState& pending) {
    pending = api::XiaomiBufferedState{};
}

} // namespace

void syncApiState(
    const State& appState,
    api::State& apiState,
    api::BufferedClient& client
) {
    for (std::size_t i = 0; i < appState.switchbotSensors.size(); ++i) {
        const auto& sensor = appState.switchbotSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.switchbot.lastSent[i];

        if (!api::shouldSendSwitchbot(current, lastSent)) {
            continue;
        }

        if (!hasValidApiTimestamp(current.lastSeenEpochS)) {
            logSkippedInvalidTimestamp("SwitchBot", sensor.identity);
            continue;
        }

        const api::WriteResult response =
            client.postSwitchbotReading(sensor.identity, current);

        logApiWriteResult("SwitchBot", sensor.identity, response);

        if (response.status == api::WriteStatus::Buffered) {
            lastSent = current;
            continue;
        }

        if (response.status != api::WriteStatus::Sent) {
            continue;
        }

        if (isAccepted(response.backendResult)) {
            lastSent = current;
        } else if (response.backendResult == api::BackendWriteResult::Conflict) {
            logConflict("SwitchBot", sensor.identity, response);
            lastSent = SwitchbotReading{};
        }
    }

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    for (std::size_t i = 0; i < appState.xiaomiSensors.size(); ++i) {
        const auto& sensor = appState.xiaomiSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.xiaomi.lastSent[i];
        auto& pending = apiState.xiaomi.pending[i];

        if (current.hasAnyValue() && !hasValidApiTimestamp(current.lastSeenEpochS)) {
            logSkippedInvalidTimestamp("Xiaomi", sensor.identity);
            resetPending(pending);
            continue;
        }

        if (current.hasAnyValue() && differsFromLastSent(current, lastSent)) {
            if (!pending.active) {
                pending.active = true;
                pending.openedAtEpochS = *current.lastSeenEpochS;
            }

            if (!sameBufferedData(current, pending.reading)) {
                pending.reading = current;
            }
        }

        if (!pending.active) {
            continue;
        }

        const bool flushDueToComplete = hasCompleteXiaomiReading(pending.reading);
        const bool flushDueToTimeout =
            now >= pending.openedAtEpochS + kXiaomiBufferWindowSeconds;

        if (!flushDueToComplete && !flushDueToTimeout) {
            continue;
        }

        if (!hasValidApiTimestamp(pending.reading.lastSeenEpochS)) {
            logSkippedInvalidTimestamp("Xiaomi", sensor.identity);
            resetPending(pending);
            continue;
        }

        const api::WriteResult response =
            client.postXiaomiReading(sensor.identity, pending.reading);

        logApiWriteResult("Xiaomi", sensor.identity, response);

        if (response.status == api::WriteStatus::Buffered) {
            lastSent = pending.reading;
            resetPending(pending);
            continue;
        }

        if (response.status != api::WriteStatus::Sent) {
            continue;
        }

        if (isAccepted(response.backendResult)) {
            lastSent = pending.reading;
            resetPending(pending);
        } else if (response.backendResult == api::BackendWriteResult::Conflict) {
            logConflict("Xiaomi", sensor.identity, response);
            lastSent = XiaomiReading{};
            resetPending(pending);
        }
    }
}
