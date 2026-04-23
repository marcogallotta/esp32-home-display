#include "update.h"

#include <optional>
#include <string>

#include "network.h"
#include "forecast/openmeteo.h"
#include "platform.h"
#include "salah/service.h"
#include "salah/state.h"

void updateSalahState(
    const Config& config,
    const std::tm& localTime,
    int& oldDay,
    salah::Schedule& today,
    salah::Schedule& tomorrow,
    State& state
) {
    if (localTime.tm_mday != oldDay) {
        salah::Schedule newToday;
        salah::Schedule newTomorrow;
        if (!salah::computeSchedules(localTime, config, newToday, newTomorrow)) {
            platform::printLine("Error computing salah schedules");
            return;
        }
        today = newToday;
        tomorrow = newTomorrow;
        oldDay = localTime.tm_mday;
    }

    state.salah =
        salah::computeState(today, tomorrow, salah::minutesSinceMidnight(localTime));
    state.hasSalah = true;
}

void updateSwitchbotState(
    const Config& config,
    const std::time_t now,
    switchbot::Scanner& scanner,
    State& state
) {
    scanner.poll();
    const auto sensors = scanner.snapshot();

    for (std::size_t i = 0; i < state.switchbotSensors.size(); ++i) {
        const auto& sensorConfig = config.switchbot.sensors[i];
        auto& row = state.switchbotSensors[i];

        row.identity.mac = sensorConfig.mac;
        row.identity.name = sensorConfig.name;
        row.identity.shortName = sensorConfig.shortName;

        const auto it = sensors.find(sensorConfig.mac);
        if (it == sensors.end()) {
            row.reading = SwitchbotReading{};
            continue;
        }

        const auto& reading = it->second;
        row.reading.temperatureC = reading.temperature_c;
        row.reading.humidityPct = reading.humidity;
        row.reading.lastSeenEpochS = reading.last_seen_epoch_s;
        row.reading.rssi = reading.rssi;
    }

    platform::printLine("Sensors:");
    for (const auto& row : state.switchbotSensors) {
        if (!row.reading.hasCompleteReading()) {
#ifdef ARDUINO
            platform::printLine(row.identity.shortName + ": no reading yet");
#else
            platform::printLine(row.identity.name + ": no reading yet");
#endif
            continue;
        }

#ifdef ARDUINO
        const std::string label = row.identity.shortName.empty()
            ? "?"
            : std::string(1, row.identity.shortName[0]);
#else
        const std::string label = row.identity.name;
#endif

        platform::printLine(
            label + ": " +
            std::to_string(*row.reading.temperatureC) + "C, " +
            std::to_string(static_cast<int>(*row.reading.humidityPct)) + "%, " +
            std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m, " +
            "rssi=" + std::to_string(*row.reading.rssi)
        );
    }
    platform::printLine("");
}

void updateXiaomiState(
    const Config& config,
    const std::time_t now,
    xiaomi::Scanner& scanner,
    State& state
) {
    scanner.poll();
    const auto sensors = scanner.snapshot();

    for (std::size_t i = 0; i < state.xiaomiSensors.size(); ++i) {
        const auto& sensorConfig = config.xiaomi.sensors[i];
        auto& row = state.xiaomiSensors[i];

        row.identity.mac = sensorConfig.mac;
        row.identity.name = sensorConfig.name;
        row.identity.shortName = sensorConfig.shortName;

        const auto it = sensors.find(sensorConfig.mac);
        if (it == sensors.end()) {
            row.reading = XiaomiReading{};
            continue;
        }

        const auto& reading = it->second;

        row.reading.temperatureC =
            reading.hasTemperature ? std::optional<float>(reading.temperatureC) : std::nullopt;
        row.reading.moisturePct =
            reading.hasMoisture ? std::optional<std::uint8_t>(reading.moisturePct) : std::nullopt;
        row.reading.lux =
            reading.hasLux ? std::optional<int>(reading.lux) : std::nullopt;
        row.reading.conductivityUsCm =
            reading.hasConductivity ? std::optional<int>(reading.conductivityUsCm) : std::nullopt;
        row.reading.lastSeenEpochS = reading.lastSeenEpochS;
        row.reading.rssi = reading.rssi;
    }

    platform::printLine("Xiaomi:");
    for (const auto& row : state.xiaomiSensors) {
        if (!row.reading.hasAnyValue()) {
#ifdef ARDUINO
            platform::printLine(row.identity.shortName + ": no reading yet");
#else
            platform::printLine(row.identity.name + ": no reading yet");
#endif
            continue;
        }

#ifdef ARDUINO
        const std::string label = row.identity.shortName.empty()
            ? "?"
            : std::string(1, row.identity.shortName[0]);
#else
        const std::string label = row.identity.name;
#endif

        std::string line = label + ": ";
        bool first = true;

        auto appendPart = [&](const std::string& part) {
            if (!first) {
                line += ", ";
            }
            line += part;
            first = false;
        };

        if (row.reading.temperatureC.has_value()) {
            appendPart(std::to_string(*row.reading.temperatureC) + "C");
        }
        if (row.reading.moisturePct.has_value()) {
            appendPart(std::to_string(static_cast<int>(*row.reading.moisturePct)) + "%");
        }
        if (row.reading.lux.has_value()) {
            appendPart("lux=" + std::to_string(*row.reading.lux));
        }
        if (row.reading.conductivityUsCm.has_value()) {
            appendPart("cond=" + std::to_string(*row.reading.conductivityUsCm));
        }
        if (row.reading.lastSeenEpochS.has_value()) {
            appendPart(std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m");
        }
        if (row.reading.rssi.has_value()) {
            appendPart("rssi=" + std::to_string(*row.reading.rssi));
        }

        platform::printLine(line);
    }
    platform::printLine("");
}

bool updateForecastState(const Config& config, State& state) {
    auto& p = network::platform(config.wifi);

    network::Request request;
    request.method = network::Method::Get;
    request.url = forecast::openmeteoUrl(config.location);
    request.pem = config.forecast.openmeteoPem;

    const auto r = p.request(request);

    if (r.transport != network::TransportResult::Ok) {
        p.log("Forecast transport failed: error=" + r.error);
        return false;
    }

    if (r.statusCode != 200) {
        p.log("Forecast HTTP failed: status=" + std::to_string(r.statusCode));
        return false;
    }

    forecast::ForecastData data;
    if (!forecast::parseForecastJson(r.body, data)) {
        p.log("Forecast JSON parse failed");
        return false;
    }

    p.log("Forecast OK: count=" + std::to_string(data.count));

    state.forecast = data;
    state.hasForecast = true;
    return true;
}
