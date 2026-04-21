#include "api_sync.h"

#include "api/payloads.h"
#include "platform.h"

namespace {

bool isAccepted(api::WriteResult result) {
    return result == api::WriteResult::Created ||
           result == api::WriteResult::Duplicate ||
           result == api::WriteResult::Merged;
}

void logConflict(
    const char* sensorType,
    const SensorIdentity& identity,
    const api::WriteResponse& response
) {
    platform::printLine(
        std::string("WARNING: API conflict for ") +
        sensorType +
        " sensor " +
        identity.name +
        " (" + identity.mac + ")" +
        " status=" + std::to_string(response.statusCode) +
        " body=" + response.body
    );
}

} // namespace

void syncApiState(
    const State& appState,
    api::State& apiState,
    const api::Client& client
) {
    for (std::size_t i = 0; i < appState.switchbotSensors.size(); ++i) {
        const auto& sensor = appState.switchbotSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.switchbot.lastSent[i];

        if (!api::shouldSendSwitchbot(current, lastSent)) {
            continue;
        }

        const auto payload = api::makeSwitchbotPayload(sensor.identity, current);
        if (!payload.has_value()) {
            continue;
        }

        const api::WriteResponse response = client.postSwitchbotReading(*payload);
        if (isAccepted(response.result)) {
            lastSent = current;
        } else if (response.result == api::WriteResult::Conflict) {
            logConflict("switchbot", sensor.identity, response);
            lastSent = SwitchbotReading{};
        }
    }

    for (std::size_t i = 0; i < appState.xiaomiSensors.size(); ++i) {
        const auto& sensor = appState.xiaomiSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.xiaomi.lastSent[i];
        bool sensorConflicted = false;

        if (api::shouldSendXiaomiTemperature(current, lastSent)) {
            const auto payload = api::makeXiaomiTemperaturePayload(sensor.identity, current);
            if (payload.has_value()) {
                const api::WriteResponse response = client.postXiaomiReading(*payload);
                if (isAccepted(response.result)) {
                    lastSent.temperatureC = current.temperatureC;
                    lastSent.lastSeenEpochS = current.lastSeenEpochS;
                } else if (response.result == api::WriteResult::Conflict) {
                    logConflict("xiaomi", sensor.identity, response);
                    lastSent = XiaomiReading{};
                    sensorConflicted = true;
                }
            }
        }

        if (sensorConflicted) {
            continue;
        }

        if (api::shouldSendXiaomiMoisture(current, lastSent)) {
            const auto payload = api::makeXiaomiMoisturePayload(sensor.identity, current);
            if (payload.has_value()) {
                const api::WriteResponse response = client.postXiaomiReading(*payload);
                if (isAccepted(response.result)) {
                    lastSent.moisturePct = current.moisturePct;
                    lastSent.lastSeenEpochS = current.lastSeenEpochS;
                } else if (response.result == api::WriteResult::Conflict) {
                    logConflict("xiaomi", sensor.identity, response);
                    lastSent = XiaomiReading{};
                    sensorConflicted = true;
                }
            }
        }

        if (sensorConflicted) {
            continue;
        }

        if (api::shouldSendXiaomiLux(current, lastSent)) {
            const auto payload = api::makeXiaomiLuxPayload(sensor.identity, current);
            if (payload.has_value()) {
                const api::WriteResponse response = client.postXiaomiReading(*payload);
                if (isAccepted(response.result)) {
                    lastSent.lux = current.lux;
                    lastSent.lastSeenEpochS = current.lastSeenEpochS;
                } else if (response.result == api::WriteResult::Conflict) {
                    logConflict("xiaomi", sensor.identity, response);
                    lastSent = XiaomiReading{};
                    sensorConflicted = true;
                }
            }
        }

        if (sensorConflicted) {
            continue;
        }

        if (api::shouldSendXiaomiConductivity(current, lastSent)) {
            const auto payload = api::makeXiaomiConductivityPayload(sensor.identity, current);
            if (payload.has_value()) {
                const api::WriteResponse response = client.postXiaomiReading(*payload);
                if (isAccepted(response.result)) {
                    lastSent.conductivityUsCm = current.conductivityUsCm;
                    lastSent.lastSeenEpochS = current.lastSeenEpochS;
                } else if (response.result == api::WriteResult::Conflict) {
                    logConflict("xiaomi", sensor.identity, response);
                    lastSent = XiaomiReading{};
                }
            }
        }
    }
}
