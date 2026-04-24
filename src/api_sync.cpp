#include "api_sync.h"

#include <cstdint>
#include <ctime>
#include <string>

#include "platform.h"

namespace {
constexpr std::int64_t kXiaomiBufferWindowSeconds = 60;

bool isAccepted(api::BackendWriteResult result) {
    return result == api::BackendWriteResult::Created ||
           result == api::BackendWriteResult::Duplicate ||
           result == api::BackendWriteResult::Merged;
}

void logConflict(
    const char* sensorType,
    const SensorIdentity& identity,
    const api::WriteResult& response
) {
    platform::printLine(
        std::string("WARNING: API conflict for ") +
        sensorType +
        " sensor " +
        identity.name +
        " (" + identity.mac + ")" +
        " status=" + std::to_string(response.httpStatusCode) +
        " body=" + response.body
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

        const api::WriteResult response =
            client.postSwitchbotReading(sensor.identity, current);

        if (response.status != api::WriteStatus::Sent) {
            continue;
        }

        if (isAccepted(response.backendResult)) {
            lastSent = current;
        } else if (response.backendResult == api::BackendWriteResult::Conflict) {
            logConflict("switchbot", sensor.identity, response);
            lastSent = SwitchbotReading{};
        }
    }

    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    for (std::size_t i = 0; i < appState.xiaomiSensors.size(); ++i) {
        const auto& sensor = appState.xiaomiSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.xiaomi.lastSent[i];
        auto& pending = apiState.xiaomi.pending[i];

        if (current.hasAnyValue() && differsFromLastSent(current, lastSent)) {
            if (!pending.active) {
                pending.active = true;
                pending.openedAtEpochS = current.lastSeenEpochS.value_or(now);
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

        const api::WriteResult response =
            client.postXiaomiReading(sensor.identity, pending.reading);

        if (response.status != api::WriteStatus::Sent) {
            continue;
        }

        if (isAccepted(response.backendResult)) {
            lastSent = pending.reading;
            resetPending(pending);
        } else if (response.backendResult == api::BackendWriteResult::Conflict) {
            logConflict("xiaomi", sensor.identity, response);
            lastSent = XiaomiReading{};
            resetPending(pending);
        }
    }
}
