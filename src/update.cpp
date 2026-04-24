#include "update.h"

#include <optional>
#include <string>

#include "forecast/openmeteo.h"
#include "log.h"
#include "network.h"
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
            logLine(LogLevel::Error, "Failed to compute salah schedule");
            return;
        }
        today = newToday;
        tomorrow = newTomorrow;
        oldDay = localTime.tm_mday;

        logLine(LogLevel::Info, "Salah schedule updated for day " + std::to_string(oldDay));
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

    for (const auto& row : state.switchbotSensors) {
#ifdef ARDUINO
        const std::string label = row.identity.shortName;
#else
        const std::string label = row.identity.name;
#endif

        if (!row.reading.hasCompleteReading()) {
            logLine(LogLevel::Info, "SwitchBot " + label + " has no reading yet");
            continue;
        }

        logLine(
            LogLevel::Info,
            "SwitchBot " + label +
            ": " + std::to_string(*row.reading.temperatureC) + "C" +
            ", " + std::to_string(static_cast<int>(*row.reading.humidityPct)) + "%" +
            ", age " + std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m" +
            ", RSSI " + std::to_string(*row.reading.rssi)
        );
    }
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

    for (const auto& row : state.xiaomiSensors) {
#ifdef ARDUINO
        const std::string label = row.identity.shortName;
#else
        const std::string label = row.identity.name;
#endif

        if (!row.reading.hasAnyValue()) {
            logLine(LogLevel::Info, "Xiaomi " + label + " has no reading yet");
            continue;
        }

        std::string msg = "Xiaomi " + label + ":";

        if (row.reading.temperatureC.has_value()) {
            msg += " " + std::to_string(*row.reading.temperatureC) + "C";
        }
        if (row.reading.moisturePct.has_value()) {
            msg += ", moisture " + std::to_string(static_cast<int>(*row.reading.moisturePct)) + "%";
        }
        if (row.reading.lux.has_value()) {
            msg += ", lux " + std::to_string(*row.reading.lux);
        }
        if (row.reading.conductivityUsCm.has_value()) {
            msg += ", conductivity " + std::to_string(*row.reading.conductivityUsCm);
        }
        if (row.reading.lastSeenEpochS.has_value()) {
            msg += ", age " + std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m";
        }
        if (row.reading.rssi.has_value()) {
            msg += ", RSSI " + std::to_string(*row.reading.rssi);
        }

        logLine(LogLevel::Info, msg);
    }
}

bool updateForecastState(const Config& config, State& state) {
    auto& p = network::platform(config.wifi);

    network::Request request;
    request.method = network::Method::Get;
    request.url = forecast::openmeteoUrl(config.location);
    request.pem = config.forecast.openmeteoPem;

    const auto r = p.request(request);

    if (r.transport != network::TransportResult::Ok) {
        logLine(
            LogLevel::Warn,
            "Forecast request failed: " + transportResultName(r.transport) +
            ", " + r.error
        );
        return false;
    }

    if (r.statusCode != 200) {
        logLine(
            LogLevel::Warn,
            "Forecast request failed with HTTP " + std::to_string(r.statusCode)
        );
        return false;
    }

    forecast::ForecastData data;
    if (!forecast::parseForecastJson(r.body, data)) {
        logLine(LogLevel::Warn, "Failed to parse forecast JSON");
        return false;
    }

    state.forecast = data;
    state.hasForecast = true;

    logLine(LogLevel::Info, "Forecast updated: " + std::to_string(data.count) + " days");
    return true;
}
