#include "api_sync.h"

#include "api/payloads.h"

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

        const bool ok = client.postSwitchbotReading(*payload);
        (void)ok; // failures are treated as dropped for now
        lastSent = current;
    }

    for (std::size_t i = 0; i < appState.xiaomiSensors.size(); ++i) {
        const auto& sensor = appState.xiaomiSensors[i];
        const auto& current = sensor.reading;
        auto& lastSent = apiState.xiaomi.lastSent[i];

        if (api::shouldSendXiaomiTemperature(current, lastSent)) {
            const auto payload = api::makeXiaomiTemperaturePayload(sensor.identity, current);
            if (payload.has_value()) {
                const bool ok = client.postXiaomiReading(*payload);
                (void)ok;
                lastSent.temperatureC = current.temperatureC;
                lastSent.lastSeenEpochS = current.lastSeenEpochS;
            }
        }

        if (api::shouldSendXiaomiMoisture(current, lastSent)) {
            const auto payload = api::makeXiaomiMoisturePayload(sensor.identity, current);
            if (payload.has_value()) {
                const bool ok = client.postXiaomiReading(*payload);
                (void)ok;
                lastSent.moisturePct = current.moisturePct;
                lastSent.lastSeenEpochS = current.lastSeenEpochS;
            }
        }

        if (api::shouldSendXiaomiLux(current, lastSent)) {
            const auto payload = api::makeXiaomiLuxPayload(sensor.identity, current);
            if (payload.has_value()) {
                const bool ok = client.postXiaomiReading(*payload);
                (void)ok;
                lastSent.lux = current.lux;
                lastSent.lastSeenEpochS = current.lastSeenEpochS;
            }
        }

        if (api::shouldSendXiaomiConductivity(current, lastSent)) {
            const auto payload = api::makeXiaomiConductivityPayload(sensor.identity, current);
            if (payload.has_value()) {
                const bool ok = client.postXiaomiReading(*payload);
                (void)ok;
                lastSent.conductivityUsCm = current.conductivityUsCm;
                lastSent.lastSeenEpochS = current.lastSeenEpochS;
            }
        }
    }
}
