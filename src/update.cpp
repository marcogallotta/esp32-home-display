#include "update.h"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <string>

#include "forecast/openmeteo.h"
#include "log.h"
#include "network.h"
#include "salah/service.h"
#include "salah/state.h"

namespace {

std::string formatFloat1(float value) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return std::string(buf);
}

std::string formatDate(const std::tm& time) {
    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << (time.tm_year + 1900) << '-'
        << std::setw(2) << (time.tm_mon + 1) << '-'
        << std::setw(2) << time.tm_mday;
    return out.str();
}

std::optional<std::int64_t> validEpochOrNull(std::int64_t epochS) {
    if (epochS <= 0) {
        return std::nullopt;
    }
    return epochS;
}

std::string switchbotLabel(const SwitchbotSensorState& row) {
#ifdef ARDUINO
    return row.identity.shortName;
#else
    return row.identity.name;
#endif
}

std::string xiaomiLabel(const XiaomiSensorState& row) {
#ifdef ARDUINO
    return row.identity.shortName;
#else
    return row.identity.name;
#endif
}

void logSwitchbotSummary(const State& state, std::time_t now) {
    int missing = 0;
    std::string msg = "SwitchBot readings:";

    for (const auto& row : state.switchbotSensors) {
        const std::string label = switchbotLabel(row);

        if (!row.reading.hasCompleteReading()) {
            missing += 1;
            continue;
        }

        msg += " " + label +
            "=" + formatFloat1(*row.reading.temperatureC) + "C/" +
            std::to_string(static_cast<int>(*row.reading.humidityPct)) + "%";

        if (row.reading.lastSeenEpochS.has_value()) {
            msg += "/" + std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m";
        } else {
            msg += "/?m";
        }


        msg += ";";
    }

    if (missing > 0) {
        msg += " missing " + std::to_string(missing);
    }

    logLine(LogLevel::Info, msg);
}

void logXiaomiSummary(const State& state, std::time_t now) {
    int missing = 0;
    std::string msg = "Xiaomi readings:";

    for (const auto& row : state.xiaomiSensors) {
        const std::string label = xiaomiLabel(row);

        if (!row.reading.hasAnyValue()) {
            missing += 1;
            continue;
        }

        msg += " " + label + "=";

        bool first = true;
        auto appendPart = [&](const std::string& part) {
            if (!first) {
                msg += "/";
            }
            msg += part;
            first = false;
        };

        if (row.reading.temperatureC.has_value()) {
            appendPart(formatFloat1(*row.reading.temperatureC) + "C");
        }
        if (row.reading.moisturePct.has_value()) {
            appendPart("moisture " + std::to_string(static_cast<int>(*row.reading.moisturePct)) + "%");
        }
        if (row.reading.lux.has_value()) {
            appendPart("lux " + std::to_string(*row.reading.lux));
        }
        if (row.reading.conductivityUsCm.has_value()) {
            appendPart("cond " + std::to_string(*row.reading.conductivityUsCm));
        }
        if (row.reading.lastSeenEpochS.has_value()) {
            appendPart(std::to_string((now - *row.reading.lastSeenEpochS) / 60) + "m");
        }

        msg += ";";
    }

    if (missing > 0) {
        msg += " missing " + std::to_string(missing);
    }

    logLine(LogLevel::Info, msg);
}

} // namespace

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

        logLine(LogLevel::Info, "Salah schedule updated for " + formatDate(localTime));
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
        row.reading.lastSeenEpochS = validEpochOrNull(reading.last_seen_epoch_s);
    }

    logSwitchbotSummary(state, now);
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
        row.reading.lastSeenEpochS = validEpochOrNull(reading.lastSeenEpochS);
    }

    logXiaomiSummary(state, now);
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
