#include "history_backend.h"

#include "../network.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace switchbot {
namespace history {
namespace {

constexpr std::uint32_t kSecondsPerMinute = 60;
constexpr std::uint32_t kSecondsPerHour = 60 * kSecondsPerMinute;
constexpr std::uint32_t kSecondsPerDay = 24 * kSecondsPerHour;

bool isLeapYear(int year) {
    if (year % 400 == 0) {
        return true;
    }
    if (year % 100 == 0) {
        return false;
    }
    return year % 4 == 0;
}

int daysInMonth(int year, int month) {
    static constexpr int kDaysByMonth[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31,
    };

    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return kDaysByMonth[month - 1];
}

std::optional<std::uint32_t> daysSinceUnixEpoch(int year, int month, int day) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month)) {
        return std::nullopt;
    }

    std::uint32_t days = 0;
    for (int y = 1970; y < year; ++y) {
        days += isLeapYear(y) ? 366U : 365U;
    }
    for (int m = 1; m < month; ++m) {
        days += static_cast<std::uint32_t>(daysInMonth(year, m));
    }
    days += static_cast<std::uint32_t>(day - 1);
    return days;
}

std::optional<std::uint32_t> makeUtcEpoch(int year, int month, int day, int hour, int minute, int second) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }

    const auto days = daysSinceUnixEpoch(year, month, day);
    if (!days.has_value()) {
        return std::nullopt;
    }

    const std::uint64_t epoch =
        static_cast<std::uint64_t>(*days) * kSecondsPerDay +
        static_cast<std::uint64_t>(hour) * kSecondsPerHour +
        static_cast<std::uint64_t>(minute) * kSecondsPerMinute +
        static_cast<std::uint64_t>(second);

    if (epoch > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(epoch);
}

bool hasUtcSuffix(const std::string& timestamp) {
    if (timestamp.size() == 20) {
        return timestamp[19] == 'Z';
    }
    return timestamp.size() > 20 && timestamp.back() == 'Z' && timestamp[19] == '.';
}

bool parseInt2(const std::string& text, std::size_t offset, int& out) {
    if (offset + 2 > text.size() ||
        !std::isdigit(static_cast<unsigned char>(text[offset])) ||
        !std::isdigit(static_cast<unsigned char>(text[offset + 1]))) {
        return false;
    }
    out = (text[offset] - '0') * 10 + (text[offset + 1] - '0');
    return true;
}

bool parseInt4(const std::string& text, std::size_t offset, int& out) {
    if (offset + 4 > text.size()) {
        return false;
    }
    out = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = text[offset + i];
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        out = out * 10 + (c - '0');
    }
    return true;
}

std::optional<std::string> optionalString(JsonVariantConst value) {
    if (value.isNull()) {
        return std::nullopt;
    }
    if (!value.is<const char*>()) {
        return std::nullopt;
    }
    return std::string(value.as<const char*>());
}

bool parseTimestampField(JsonVariantConst value,
                         std::optional<std::string>& timestamp,
                         std::optional<std::uint32_t>& epoch,
                         std::string& error,
                         const char* fieldName) {
    timestamp = optionalString(value);
    epoch.reset();

    if (!timestamp.has_value()) {
        return value.isNull();
    }

    epoch = parseIsoUtcEpoch(*timestamp);
    if (!epoch.has_value()) {
        error = std::string("invalid timestamp field ") + fieldName + ": " + *timestamp;
        return false;
    }
    return true;
}

std::string apiUrl(const Config& config, const char* path) {
    if (config.api.baseUrl.empty() || config.api.baseUrl.back() != '/') {
        return config.api.baseUrl + path;
    }
    return config.api.baseUrl.substr(0, config.api.baseUrl.size() - 1) + path;
}

std::uint32_t clampWindowStart(std::uint32_t start,
                               std::uint32_t nowEpoch,
                               const HistoryPlanningOptions& options,
                               bool& clamped) {
    if (options.historyLimitSeconds == 0 || nowEpoch <= options.historyLimitSeconds) {
        return start;
    }

    const std::uint32_t earliestAllowed = nowEpoch - options.historyLimitSeconds;
    if (start < earliestAllowed) {
        clamped = true;
        return earliestAllowed;
    }
    return start;
}

std::uint32_t alignEpochCeil(std::uint32_t epoch, std::uint32_t intervalSeconds) {
    if (intervalSeconds == 0) {
        return epoch;
    }

    const std::uint32_t remainder = epoch % intervalSeconds;
    if (remainder == 0) {
        return epoch;
    }
    return epoch + (intervalSeconds - remainder);
}

PlannedHistoryWindow makePlannedWindow(const std::string& source,
                                       std::uint32_t rawStartEpoch,
                                       std::uint32_t rawEndEpoch,
                                       std::uint32_t nowEpoch,
                                       const HistoryPlanningOptions& options) {
    PlannedHistoryWindow window;
    window.source = source;
    window.endEpoch = std::min(rawEndEpoch, nowEpoch);
    window.startEpoch = clampWindowStart(rawStartEpoch, nowEpoch, options, window.clampedToHistoryLimit);

    if (window.endEpoch <= window.startEpoch || options.sampleIntervalSeconds == 0) {
        return window;
    }

    window.firstPointEpoch = alignEpochCeil(window.startEpoch, options.sampleIntervalSeconds);
    if (window.firstPointEpoch >= window.endEpoch) {
        return window;
    }

    window.pointCount = 1 + ((window.endEpoch - 1 - window.firstPointEpoch) / options.sampleIntervalSeconds);
    window.lastPointEpoch = window.firstPointEpoch + (window.pointCount - 1) * options.sampleIntervalSeconds;
    return window;
}

void addWindowIfUseful(std::vector<PlannedHistoryWindow>& windows, PlannedHistoryWindow window) {
    if (window.endEpoch <= window.startEpoch || window.pointCount == 0) {
        return;
    }
    windows.push_back(std::move(window));
}

}  // namespace

std::string normalizeMac(const std::string& mac) {
    std::string hex;
    hex.reserve(12);

    for (char c : mac) {
        if (c == ':' || c == '-' || std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return {};
        }
        hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (hex.size() != 12) {
        return {};
    }

    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < hex.size(); ++i) {
        if (i != 0 && i % 2 == 0) {
            out.push_back(':');
        }
        out.push_back(hex[i]);
    }
    return out;
}

std::optional<std::uint32_t> parseIsoUtcEpoch(const std::string& timestamp) {
    if (timestamp.size() < 20 ||
        timestamp[4] != '-' || timestamp[7] != '-' ||
        timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
        !hasUtcSuffix(timestamp)) {
        return std::nullopt;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!parseInt4(timestamp, 0, year) ||
        !parseInt2(timestamp, 5, month) ||
        !parseInt2(timestamp, 8, day) ||
        !parseInt2(timestamp, 11, hour) ||
        !parseInt2(timestamp, 14, minute) ||
        !parseInt2(timestamp, 17, second)) {
        return std::nullopt;
    }

    return makeUtcEpoch(year, month, day, hour, minute, second);
}

std::string formatIsoUtc(std::uint32_t epoch) {
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    if (gmtime_r(&t, &tm) == nullptr) {
        return {};
    }
#endif

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return {};
    }
    return std::string(buf);
}

std::string makeSensorLookupPayload(const std::vector<std::string>& normalizedMacs) {
    DynamicJsonDocument doc(512 + normalizedMacs.size() * 48);
    JsonArray sensors = doc.createNestedArray("sensors");
    for (const std::string& mac : normalizedMacs) {
        JsonObject sensor = sensors.createNestedObject();
        sensor["mac"] = mac;
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

SensorLookupResult parseSensorLookupResponse(const std::string& body, int httpStatusCode) {
    SensorLookupResult result;
    result.httpStatusCode = httpStatusCode;

    DynamicJsonDocument doc(8192);
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
        result.error = std::string("JSON parse failed: ") + err.c_str();
        return result;
    }

    const JsonArrayConst sensors = doc["sensors"].as<JsonArrayConst>();
    if (sensors.isNull()) {
        result.error = "response.sensors is missing or not an array";
        return result;
    }

    const JsonArrayConst warnings = doc["warnings"].as<JsonArrayConst>();
    if (!warnings.isNull()) {
        for (JsonVariantConst warning : warnings) {
            if (warning.is<const char*>()) {
                result.warnings.push_back(warning.as<const char*>());
            }
        }
    }

    for (JsonObjectConst item : sensors) {
        BackendSensorInfo sensor;
        const char* rawMac = item["mac"] | "";
        sensor.mac = normalizeMac(rawMac);
        sensor.sensorId = item["sensor_id"] | "";
        sensor.syncIntervalsCapped = item["sync_intervals_capped"] | false;

        if (sensor.mac.empty()) {
            result.error = "sensor item has invalid mac";
            return result;
        }
        if (sensor.sensorId.empty()) {
            result.error = "sensor item has missing sensor_id for " + sensor.mac;
            return result;
        }

        if (!parseTimestampField(item["first_timestamp"], sensor.firstTimestamp, sensor.firstEpoch, result.error, "first_timestamp") ||
            !parseTimestampField(item["latest_timestamp"], sensor.latestTimestamp, sensor.latestEpoch, result.error, "latest_timestamp")) {
            return result;
        }

        const JsonArrayConst intervals = item["sync_intervals"].as<JsonArrayConst>();
        if (!intervals.isNull()) {
            for (JsonObjectConst intervalJson : intervals) {
                const char* start = intervalJson["start"] | "";
                const char* end = intervalJson["end"] | "";
                const auto startEpoch = parseIsoUtcEpoch(start);
                const auto endEpoch = parseIsoUtcEpoch(end);
                if (!startEpoch.has_value() || !endEpoch.has_value()) {
                    result.error = "invalid sync interval timestamp for " + sensor.mac;
                    return result;
                }

                BackendSyncInterval interval;
                interval.startTimestamp = start;
                interval.endTimestamp = end;
                interval.startEpoch = *startEpoch;
                interval.endEpoch = *endEpoch;
                sensor.syncIntervals.push_back(std::move(interval));
            }
        }

        result.sensors.push_back(std::move(sensor));
    }

    result.ok = true;
    return result;
}

SensorLookupResult postSensorLookup(const Config& config, const std::vector<std::string>& normalizedMacs) {
    const std::string payload = makeSensorLookupPayload(normalizedMacs);
    const network::Headers headers{{"x-api-key", config.api.apiKey}};
    const network::HttpResponse response = network::platform(config.wifi).httpPost(
        apiUrl(config, "/switchbot/sensors"),
        payload,
        config.api.pem,
        "application/json",
        headers
    );

    if (response.transport != network::TransportResult::Ok) {
        SensorLookupResult result;
        result.httpStatusCode = response.statusCode;
        result.error = "transport_error: " + response.error;
        return result;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        SensorLookupResult result;
        result.httpStatusCode = response.statusCode;
        result.error = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return result;
    }

    return parseSensorLookupResponse(response.body, response.statusCode);
}

std::string makeBulkUploadPayload(const std::string& sensorId, const std::vector<BulkHistoryReading>& readings) {
    DynamicJsonDocument doc(256 + readings.size() * 96);
    doc["sensor_id"] = sensorId;
    JsonArray out = doc.createNestedArray("readings");

    for (const BulkHistoryReading& reading : readings) {
        JsonObject item = out.createNestedObject();
        item["timestamp"] = formatIsoUtc(reading.timestampEpoch);
        item["temperature_c"] = reading.temperatureC;
        item["humidity_pct"] = reading.humidityPct;
    }

    std::string payload;
    serializeJson(doc, payload);
    return payload;
}

BulkUploadResult parseBulkUploadResponse(const std::string& body, int httpStatusCode) {
    BulkUploadResult result;
    result.ok = true;
    result.httpStatusCode = httpStatusCode;

    if (body.empty()) {
        return result;
    }

    DynamicJsonDocument doc(4096);
    const DeserializationError err = deserializeJson(doc, body);
    if (err) {
        result.ok = false;
        result.error = std::string("JSON parse failed: ") + err.c_str();
        return result;
    }

    const JsonArrayConst errors = doc["errors"].as<JsonArrayConst>();
    if (!errors.isNull()) {
        for (JsonObjectConst item : errors) {
            BulkUploadError error;
            error.index = item["index"] | 0U;
            error.code = item["code"] | "";
            error.message = item["message"] | "";
            result.errors.push_back(std::move(error));
        }
    }

    return result;
}

BulkUploadResult postBulkUpload(const Config& config,
                                const std::string& sensorId,
                                const std::vector<BulkHistoryReading>& readings) {
    if (sensorId.empty()) {
        BulkUploadResult result;
        result.error = "missing sensor_id";
        return result;
    }
    if (readings.empty()) {
        BulkUploadResult result;
        result.ok = true;
        return result;
    }

    const std::string payload = makeBulkUploadPayload(sensorId, readings);
    const network::Headers headers{{"x-api-key", config.api.apiKey}};
    const network::HttpResponse response = network::platform(config.wifi).httpPost(
        apiUrl(config, "/switchbot/bulk"),
        payload,
        config.api.pem,
        "application/json",
        headers
    );

    if (response.transport != network::TransportResult::Ok) {
        BulkUploadResult result;
        result.httpStatusCode = response.statusCode;
        result.error = "transport_error: " + response.error;
        return result;
    }

    if (response.statusCode < 200 || response.statusCode >= 300) {
        BulkUploadResult result;
        result.httpStatusCode = response.statusCode;
        result.error = "HTTP " + std::to_string(response.statusCode) + ": " + response.body;
        return result;
    }

    return parseBulkUploadResponse(response.body, response.statusCode);
}

std::vector<PlannedHistoryWindow> planHistoryWindows(const BackendSensorInfo& sensor,
                                                     std::uint32_t nowEpoch,
                                                     const HistoryPlanningOptions& options) {
    std::vector<PlannedHistoryWindow> windows;

    if (sensor.firstEpoch.has_value() && options.newSensorWindowSeconds > 0) {
        const std::uint32_t firstEpoch = *sensor.firstEpoch;
        const std::uint32_t start = firstEpoch > options.newSensorWindowSeconds
            ? firstEpoch - options.newSensorWindowSeconds
            : 0;
        addWindowIfUseful(
            windows,
            makePlannedWindow("leading_backfill", start, firstEpoch, nowEpoch, options)
        );
    }

    for (const BackendSyncInterval& interval : sensor.syncIntervals) {
        addWindowIfUseful(
            windows,
            makePlannedWindow("internal_gap", interval.startEpoch, interval.endEpoch, nowEpoch, options)
        );
    }

    if (sensor.latestEpoch.has_value()) {
        addWindowIfUseful(
            windows,
            makePlannedWindow("trailing", *sensor.latestEpoch, nowEpoch, nowEpoch, options)
        );
    } else if (options.newSensorWindowSeconds > 0) {
        const std::uint32_t start = nowEpoch > options.newSensorWindowSeconds
            ? nowEpoch - options.newSensorWindowSeconds
            : 0;
        addWindowIfUseful(
            windows,
            makePlannedWindow("new_sensor", start, nowEpoch, nowEpoch, options)
        );
    }

    return windows;
}

namespace {

std::uint32_t absDistance(std::uint32_t a, std::uint32_t b) {
    return a > b ? a - b : b - a;
}

} // namespace

std::vector<BulkHistoryReading> selectAlignedReadings(const std::vector<Sample>& samples,
                                                      const PlannedHistoryWindow& window,
                                                      std::uint32_t sampleIntervalSeconds,
                                                      std::uint32_t deviceIntervalSeconds) {
    std::vector<BulkHistoryReading> out;
    if (window.pointCount == 0 || sampleIntervalSeconds == 0 || samples.empty()) {
        return out;
    }

    out.reserve(window.pointCount);
    const std::uint32_t tolerance = std::max<std::uint32_t>(1, deviceIntervalSeconds / 2);
    std::size_t searchFrom = 0;

    for (std::uint32_t target = window.firstPointEpoch;
         target <= window.lastPointEpoch;
         target += sampleIntervalSeconds) {
        std::size_t bestIndex = samples.size();
        std::uint32_t bestDistance = std::numeric_limits<std::uint32_t>::max();

        while (searchFrom < samples.size() && samples[searchFrom].epoch + tolerance < target) {
            ++searchFrom;
        }

        for (std::size_t i = searchFrom; i < samples.size(); ++i) {
            const std::uint32_t distance = absDistance(samples[i].epoch, target);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
            if (samples[i].epoch > target && distance > tolerance) {
                break;
            }
        }

        if (bestIndex != samples.size() && bestDistance <= tolerance) {
            BulkHistoryReading reading;
            reading.timestampEpoch = target;
            reading.temperatureC = samples[bestIndex].temperatureC;
            reading.humidityPct = samples[bestIndex].humidityPct;
            out.push_back(reading);
        }

        if (target > std::numeric_limits<std::uint32_t>::max() - sampleIntervalSeconds) {
            break;
        }
    }

    return out;
}

}  // namespace history
}  // namespace switchbot
