#include "update.h"

#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <map>
#include <optional>
#include <string>

#include <ArduinoJson.h>

#include "forecast/openmeteo.h"
#include "log.h"
#include "network.h"
#include "salah/service.h"
#include "salah/state.h"
#include "time_utils.h"

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

std::optional<std::int64_t> parseIso8601Utc(const char* s) {
    if (s == nullptr) return std::nullopt;
    // Parse "YYYY-MM-DDTHH:MM:SS" -- timezone suffix is ignored (server always UTC).
    int y, mo, d, h, mi, sec;
    if (std::sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return std::nullopt;
    }
    const std::int64_t epoch = time_utils::utcBrokenDownToEpoch(y, mo, d, h, mi, sec);
    if (epoch < 0) return std::nullopt;
    return epoch;
}

std::string switchbotLabel(const SwitchbotSensorState& row) {
    return row.identity.name;
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

bool updateSwitchbotFromBackend(
    const Config& config,
    std::time_t now,
    State& state
) {
    auto& net = network::platform(config.wifi);

    std::string url = config.api.baseUrl;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/sensors/latest";

    network::Request req;
    req.method = network::Method::Get;
    req.url = url;
    req.pem = config.api.pem;
    req.headers["x-api-key"] = config.api.apiKey;

    const auto r = net.request(req);

    if (r.transport != network::TransportResult::Ok) {
        logLine(LogLevel::Warn,
            "Backend sensor fetch failed: " + transportResultName(r.transport) + ", " + r.error);
        return false;
    }
    if (r.statusCode != 200) {
        logLine(LogLevel::Warn,
            "Backend sensor fetch HTTP " + std::to_string(r.statusCode));
        return false;
    }

    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, r.body) != DeserializationError::Ok) {
        logLine(LogLevel::Warn, "Backend sensor fetch: JSON parse failed");
        return false;
    }

    const JsonArray sensors = doc["sensors"];
    if (sensors.isNull()) {
        logLine(LogLevel::Warn, "Backend sensor fetch: missing sensors array");
        return false;
    }

    // Build MAC -> reading map from the response.
    std::map<std::string, SwitchbotReading> readings;
    for (const JsonObject entry : sensors) {
        const char* mac = entry["mac"];
        if (mac == nullptr) continue;

        const JsonObject reading = entry["reading"];
        if (reading.isNull()) continue;

        SwitchbotReading rb;
        if (!reading["temperature_c"].isNull()) {
            rb.temperatureC = reading["temperature_c"].as<float>();
        }
        if (!reading["humidity_pct"].isNull()) {
            const float h = reading["humidity_pct"].as<float>();
            if (h >= 0.0f && h <= 255.0f) {
                rb.humidityPct = static_cast<std::uint8_t>(h);
            }
        }
        rb.lastSeenEpochS = parseIso8601Utc(entry["latest_timestamp"]);

        readings[mac] = rb;
    }

    for (std::size_t i = 0; i < state.switchbotSensors.size(); ++i) {
        const auto& sensorConfig = config.switchbot.sensors[i];
        auto& row = state.switchbotSensors[i];

        row.identity.mac = sensorConfig.mac;
        row.identity.name = sensorConfig.name;
        row.identity.shortName = sensorConfig.shortName;

        const auto it = readings.find(sensorConfig.mac);
        if (it == readings.end()) {
            row.reading = SwitchbotReading{};
        } else {
            row.reading = it->second;
        }
    }

    logSwitchbotSummary(state, now);
    return true;
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
