#include "update.h"

#include <cstdint>
#include <cctype>
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


std::string normalizeMacForLookup(const char* mac) {
    if (mac == nullptr) return {};

    std::string hex;
    hex.reserve(12);
    for (const char* p = mac; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (*p == ':' || *p == '-' || std::isspace(c)) {
            continue;
        }
        if (!std::isxdigit(c)) {
            return {};
        }
        hex.push_back(static_cast<char>(std::toupper(c)));
    }

    if (hex.size() != 12) return {};

    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < hex.size(); ++i) {
        if (i != 0 && i % 2 == 0) out.push_back(':');
        out.push_back(hex[i]);
    }
    return out;
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
    // Parse "YYYY-MM-DDTHH:MM:SS", optionally followed by "Z" or "+/-HH:MM".
    int y, mo, d, h, mi, sec;
    int consumed = 0;
    if (std::sscanf(s, "%d-%d-%dT%d:%d:%d%n", &y, &mo, &d, &h, &mi, &sec, &consumed) != 6) {
        return std::nullopt;
    }

    int offsetSeconds = 0;
    const char* tz = s + consumed;
    if (*tz == '\0') {
        offsetSeconds = 0;
    } else if (tz[0] == 'Z' && tz[1] == '\0') {
        offsetSeconds = 0;
    } else if (tz[0] == '+' || tz[0] == '-') {
        int tzh = 0;
        int tzm = 0;
        char tail = '\0';
        if (std::sscanf(tz + 1, "%2d:%2d%c", &tzh, &tzm, &tail) != 2) {
            return std::nullopt;
        }
        if (tzh < 0 || tzh > 23 || tzm < 0 || tzm > 59) {
            return std::nullopt;
        }
        offsetSeconds = (tzh * 60 + tzm) * 60;
        if (tz[0] == '-') offsetSeconds = -offsetSeconds;
    } else {
        return std::nullopt;
    }

    const std::int64_t localEpoch = time_utils::utcBrokenDownToEpoch(y, mo, d, h, mi, sec);
    if (localEpoch < 0) return std::nullopt;

    const std::int64_t epoch = localEpoch - offsetSeconds;
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

SwitchbotBackendUpdateResult applySwitchbotBackendResponse(
    const Config& config,
    const std::string& body,
    std::time_t now,
    State& state
) {
    SwitchbotBackendUpdateResult result;

    StaticJsonDocument<4096> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        logLine(LogLevel::Warn, "Backend sensor fetch: JSON parse failed");
        return result;
    }

    const JsonArray sensors = doc["sensors"].as<JsonArray>();
    if (sensors.isNull()) {
        logLine(LogLevel::Warn, "Backend sensor fetch: missing sensors array");
        return result;
    }

    const int retryAfterSecs = doc["retry_after_secs"].as<int>();
    if (retryAfterSecs > 0) {
        result.retryAfterSecs = retryAfterSecs;
    }

    // Exact v2 contract from the local proxy:
    // {sensors:[{mac,name,recorded_at,temperature_c,humidity_pct,stale}], retry_after_secs:int}
    std::map<std::string, SwitchbotReading> readings;
    for (JsonObject entry : sensors) {
        const std::string mac = normalizeMacForLookup(entry["mac"].as<const char*>());
        if (mac.empty()) continue;

        SwitchbotReading rb;
        if (!entry["temperature_c"].isNull()) {
            rb.temperatureC = entry["temperature_c"].as<float>();
        }
        if (!entry["humidity_pct"].isNull()) {
            const float h = entry["humidity_pct"].as<float>();
            if (h >= 0.0f && h <= 255.0f) {
                rb.humidityPct = static_cast<std::uint8_t>(h);
            }
        }
        rb.lastSeenEpochS = parseIso8601Utc(entry["recorded_at"].as<const char*>());

        readings[mac] = rb;
    }

    for (std::size_t i = 0; i < state.switchbotSensors.size(); ++i) {
        const auto& sensorConfig = config.switchbot.sensors[i];
        auto& row = state.switchbotSensors[i];

        row.identity.mac = sensorConfig.mac;
        row.identity.name = sensorConfig.name;
        row.identity.shortName = sensorConfig.shortName;

        const std::string sensorMac = normalizeMacForLookup(sensorConfig.mac.c_str());
        const auto it = readings.find(sensorMac);
        if (it == readings.end()) {
            row.reading = SwitchbotReading{};
        } else {
            row.reading = it->second;
        }
    }

    logSwitchbotSummary(state, now);
    result.ok = true;
    return result;
}

SwitchbotBackendUpdateResult updateSwitchbotFromBackend(
    const Config& config,
    std::time_t now,
    State& state
) {
    SwitchbotBackendUpdateResult result;
    auto& net = network::platform(config.wifi);

    std::string url = config.api.baseUrl;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/sensors/meter/latest";

    network::Request req;
    req.method = network::Method::Get;
    req.url = url;
    req.pem = config.api.pem;
    req.headers["x-api-key"] = config.api.apiKey;

    const auto r = net.request(req);

    if (r.transport != network::TransportResult::Ok) {
        logLine(LogLevel::Warn,
            "Backend sensor fetch failed: " + transportResultName(r.transport) + ", " + r.error);
        return result;
    }
    if (r.statusCode != 200) {
        logLine(LogLevel::Warn,
            "Backend sensor fetch HTTP " + std::to_string(r.statusCode));
        return result;
    }

    return applySwitchbotBackendResponse(config, r.body, now, state);
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
