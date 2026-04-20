#include "update.h"

#include <exception>
#include <string>
#include <tuple>

#include "forecast/network.h"
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

    platform::printLine(std::string("Current: ") + toString(state.salah.current));
    platform::printLine(std::string("Next: ") + toString(state.salah.next));
    platform::printLine(
        "Remaining: " + std::to_string(state.salah.minutesRemaining) + " min"
    );
    platform::printLine("");
}

void updateSensorState(
    const Config& config,
    const std::time_t now,
    switchbot::Scanner& scanner,
    State& state
) {
    scanner.poll();
    const auto sensors = scanner.snapshot();

    for (std::size_t i = 0; i < state.sensors.size(); ++i) {
        const auto& sensorConfig = config.switchbot.sensors[i];
        SensorRowState& row = state.sensors[i];
        row.name = sensorConfig.name;
        row.shortName = sensorConfig.shortName.empty() ? '?' : sensorConfig.shortName[0];

        const auto it = sensors.find(sensorConfig.mac);
        if (it == sensors.end()) {
            continue;
        }

        const auto& reading = it->second;
        row.hasReading = true;
        row.name = reading.name;
        row.shortName = reading.shortName.empty() ? '?' : reading.shortName[0];
        row.temperatureC = reading.temperature_c;
        row.humidity = reading.humidity;
        row.lastSeenEpochS = reading.last_seen_epoch_s;
        row.rssi = reading.rssi;
    }

    platform::printLine("Sensors:");
    for (std::size_t i = 0; i < state.sensors.size(); ++i) {
        const auto& sensorConfig = config.switchbot.sensors[i];
        const SensorRowState& row = state.sensors[i];

        if (!row.hasReading) {
#ifdef ARDUINO
            platform::printLine(sensorConfig.shortName + ": no reading yet");
#else
            platform::printLine(sensorConfig.name + ": no reading yet");
#endif
            continue;
        }

#ifdef ARDUINO
        const std::string label(1, row.shortName);
#else
        const std::string label = row.name;
#endif

        platform::printLine(
            label + ": " +
            std::to_string(row.temperatureC) + "C, " +
            std::to_string(static_cast<int>(row.humidity)) + "%, " +
            std::to_string((now - row.lastSeenEpochS) / 60) + "m, " +
            "rssi=" + std::to_string(row.rssi)
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
        XiaomiRowState& row = state.xiaomiSensors[i];
        row.name = sensorConfig.name;
        row.shortName = sensorConfig.shortName.empty() ? '?' : sensorConfig.shortName[0];

        const auto it = sensors.find(sensorConfig.mac);
        if (it == sensors.end()) {
            continue;
        }

        const auto& reading = it->second;
        row.hasReading = true;
        row.name = reading.name;
        row.shortName = reading.shortName.empty() ? '?' : reading.shortName[0];
        row.hasTemperature = reading.hasTemperature;
        row.temperatureC = reading.temperatureC;
        row.hasLux = reading.hasLux;
        row.lux = reading.lux;
        row.hasMoisture = reading.hasMoisture;
        row.moisturePct = reading.moisturePct;
        row.hasConductivity = reading.hasConductivity;
        row.conductivityUsCm = reading.conductivityUsCm;
        row.lastSeenEpochS = reading.lastSeenEpochS;
        row.rssi = reading.rssi;
    }

    platform::printLine("Xiaomi:");
    for (std::size_t i = 0; i < state.xiaomiSensors.size(); ++i) {
        const auto& sensorConfig = config.xiaomi.sensors[i];
        const XiaomiRowState& row = state.xiaomiSensors[i];

        if (!row.hasReading) {
#ifdef ARDUINO
            platform::printLine(sensorConfig.shortName + ": no reading yet");
#else
            platform::printLine(sensorConfig.name + ": no reading yet");
#endif
            continue;
        }

#ifdef ARDUINO
        const std::string label(1, row.shortName);
#else
        const std::string label = row.name;
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

        if (row.hasTemperature) {
            appendPart(std::to_string(row.temperatureC) + "C");
        }
        if (row.hasMoisture) {
            appendPart(std::to_string(static_cast<int>(row.moisturePct)) + "%");
        }
        if (row.hasLux) {
            appendPart("lux=" + std::to_string(row.lux));
        }
        if (row.hasConductivity) {
            appendPart("cond=" + std::to_string(row.conductivityUsCm));
        }

        appendPart(std::to_string((now - row.lastSeenEpochS) / 60) + "m");
        appendPart("rssi=" + std::to_string(row.rssi));

        platform::printLine(line);
    }
    platform::printLine("");
}

bool updateForecastState(const Config& config, State& state) {
    auto& p = forecast::platform(config.wifi);
    const std::string url = forecast::openmeteoUrl(config.location);
    const auto r = p.httpGet(url, config.forecast.openmeteoPem);
    if (r.status_code != 200) {
        p.log("HTTP failed: status=" + std::to_string(r.status_code) + " error=" + r.error);
        return false;
    }

    forecast::ForecastData data;
    if (!forecast::parseForecastJson(r.body, data)) {
        return false;
    }
    state.forecast = data;
    state.hasForecast = true;

    for (int i = 0; i < data.count; ++i) {
        platform::printLine(
            data.days[i].date +
            " code=" + std::to_string(data.days[i].weatherCode) +
            " max=" + std::to_string(data.days[i].tempMax) +
            " min=" + std::to_string(data.days[i].tempMin) +
            " rain=" + std::to_string(data.days[i].rainProbMax)
        );
    }
    platform::printLine("");

    return true;
}
