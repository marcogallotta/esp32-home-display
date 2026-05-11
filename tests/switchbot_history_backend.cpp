#include "switchbot/history_backend.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

switchbot::history::Sample makeSample(std::uint32_t epoch, double tempC = 20.0, std::uint8_t hum = 50) {
    switchbot::history::Sample s;
    s.epoch = epoch;
    s.temperatureC = tempC;
    s.humidityPct = hum;
    return s;
}

switchbot::history::PlannedHistoryWindow makeWindow(std::uint32_t first, std::uint32_t last, std::uint32_t count) {
    switchbot::history::PlannedHistoryWindow w;
    w.firstPointEpoch = first;
    w.lastPointEpoch = last;
    w.pointCount = count;
    return w;
}

} // namespace

TEST_CASE("switchbot history backend normalizes mac addresses") {
    CHECK_EQ(switchbot::history::normalizeMac("aa:bb:cc:dd:ee:ff"), "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(switchbot::history::normalizeMac("aabbccddeeff"), "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(switchbot::history::normalizeMac("AA-BB-CC-DD-EE-FF"), "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(switchbot::history::normalizeMac("bad-mac"), "");
}

TEST_CASE("switchbot history backend parses UTC timestamps") {
    const auto epoch = switchbot::history::parseIsoUtcEpoch("2026-05-08T14:45:00Z");
    REQUIRE(epoch.has_value());
    CHECK_EQ(*epoch, 1778251500U);
    CHECK_EQ(switchbot::history::formatIsoUtc(*epoch), "2026-05-08T14:45:00Z");

    const auto fractional = switchbot::history::parseIsoUtcEpoch("2026-05-08T14:45:00.767Z");
    REQUIRE(fractional.has_value());
    CHECK_EQ(*fractional, 1778251500U);

    CHECK_FALSE(switchbot::history::parseIsoUtcEpoch("2026-05-08 14:45:00Z").has_value());
    CHECK_FALSE(switchbot::history::parseIsoUtcEpoch("2026-02-30T14:45:00Z").has_value());
}

TEST_CASE("switchbot history backend builds sensor lookup payload") {
    const std::string payload = switchbot::history::makeSensorLookupPayload({"AA:BB:CC:DD:EE:FF"});
    CHECK(payload.find("\"mac\":\"AA:BB:CC:DD:EE:FF\"") != std::string::npos);
}

TEST_CASE("switchbot history backend parses sensor lookup response") {
    const std::string body = R"json({
        "sensors": [
            {
                "mac": "aa:bb:cc:dd:ee:ff",
                "sensor_id": "sensor-uuid",
                "first_timestamp": "2026-04-21T18:00:00Z",
                "latest_timestamp": "2026-05-08T14:45:00Z",
                "sync_intervals": [
                    {"start": "2026-04-22T01:00:00Z", "end": "2026-04-22T01:40:00Z"}
                ],
                "sync_intervals_capped": true
            }
        ],
        "warnings": ["sync_intervals_capped"]
    })json";

    const auto parsed = switchbot::history::parseSensorLookupResponse(body, 200);
    REQUIRE(parsed.ok);
    REQUIRE_EQ(parsed.sensors.size(), 1U);
    CHECK_EQ(parsed.sensors[0].mac, "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(parsed.sensors[0].sensorId, "sensor-uuid");
    REQUIRE(parsed.sensors[0].latestEpoch.has_value());
    CHECK_EQ(*parsed.sensors[0].latestEpoch, 1778251500U);
    REQUIRE_EQ(parsed.sensors[0].syncIntervals.size(), 1U);
    CHECK_EQ(parsed.sensors[0].syncIntervals[0].startEpoch, 1776819600U);
    CHECK_EQ(parsed.sensors[0].syncIntervals[0].endEpoch, 1776822000U);
    CHECK(parsed.sensors[0].syncIntervalsCapped);
    REQUIRE_EQ(parsed.warnings.size(), 1U);
}

TEST_CASE("switchbot history backend parses nullable timestamps") {
    const std::string body = R"json({
        "sensors": [
            {
                "mac": "AA:BB:CC:DD:EE:FF",
                "sensor_id": "sensor-uuid",
                "first_timestamp": null,
                "latest_timestamp": null,
                "sync_intervals": [],
                "sync_intervals_capped": false
            }
        ],
        "warnings": []
    })json";

    const auto parsed = switchbot::history::parseSensorLookupResponse(body, 200);
    REQUIRE(parsed.ok);
    REQUIRE_EQ(parsed.sensors.size(), 1U);
    CHECK_FALSE(parsed.sensors[0].firstEpoch.has_value());
    CHECK_FALSE(parsed.sensors[0].latestEpoch.has_value());
}

TEST_CASE("switchbot history backend plans quarter-hour internal and trailing windows") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";
    sensor.firstEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T10:00:00Z");
    sensor.latestEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T14:45:00Z");

    switchbot::history::BackendSyncInterval gap;
    gap.startEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T01:02:00Z");
    gap.endEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T01:40:00Z");
    sensor.syncIntervals.push_back(gap);

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 6 * 60 * 60;
    options.historyLimitSeconds = 68U * 24U * 60U * 60U;

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:20:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    REQUIRE_EQ(windows.size(), 3U);
    CHECK_EQ(windows[0].source, "leading_backfill");
    CHECK_EQ(windows[0].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T04:00:00Z"));
    CHECK_EQ(windows[0].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:45:00Z"));
    CHECK_EQ(windows[0].pointCount, 24U);

    CHECK_EQ(windows[1].source, "internal_gap");
    CHECK_EQ(windows[1].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T01:15:00Z"));
    CHECK_EQ(windows[1].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T01:30:00Z"));
    CHECK_EQ(windows[1].pointCount, 2U);

    CHECK_EQ(windows[2].source, "trailing");
    CHECK_EQ(windows[2].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T14:45:00Z"));
    CHECK_EQ(windows[2].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:15:00Z"));
    CHECK_EQ(windows[2].pointCount, 3U);
}


TEST_CASE("switchbot history backend plans leading backfill before recent first point") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";
    sensor.firstEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:10:00Z");
    sensor.latestEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:10:00Z");

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 6 * 60 * 60;
    options.historyLimitSeconds = 68U * 24U * 60U * 60U;

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:20:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    REQUIRE_EQ(windows.size(), 2U);
    CHECK_EQ(windows[0].source, "leading_backfill");
    CHECK_EQ(windows[0].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:15:00Z"));
    CHECK_EQ(windows[0].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:00:00Z"));
    CHECK_EQ(windows[0].pointCount, 24U);

    CHECK_EQ(windows[1].source, "trailing");
    CHECK_EQ(windows[1].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:15:00Z"));
    CHECK_EQ(windows[1].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:15:00Z"));
    CHECK_EQ(windows[1].pointCount, 1U);
}

TEST_CASE("switchbot history backend plans multiple internal gaps") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";
    sensor.firstEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T00:00:00Z");
    sensor.latestEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T12:00:00Z");

    switchbot::history::BackendSyncInterval gap1;
    gap1.startEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T02:00:00Z");
    gap1.endEpoch   = *switchbot::history::parseIsoUtcEpoch("2026-05-08T02:30:00Z");

    switchbot::history::BackendSyncInterval gap2;
    gap2.startEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T06:00:00Z");
    gap2.endEpoch   = *switchbot::history::parseIsoUtcEpoch("2026-05-08T06:45:00Z");

    sensor.syncIntervals = {gap1, gap2};

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 6 * 60 * 60;
    options.historyLimitSeconds = 68U * 24U * 60U * 60U;

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T13:00:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    REQUIRE_EQ(windows.size(), 4U);
    CHECK_EQ(windows[0].source, "leading_backfill");
    CHECK_EQ(windows[1].source, "internal_gap");
    CHECK_EQ(windows[1].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T02:00:00Z"));
    CHECK_EQ(windows[2].source, "internal_gap");
    CHECK_EQ(windows[2].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T06:00:00Z"));
    CHECK_EQ(windows[3].source, "trailing");
}

TEST_CASE("switchbot history backend clamps leading backfill to history limit") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";
    // firstEpoch within the limit, but newSensorWindow would extend past it
    sensor.firstEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:00:00Z");
    sensor.latestEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:00:00Z");

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 8 * 60 * 60;  // would go back to 01:00 — before limit
    options.historyLimitSeconds = 4 * 60 * 60;     // only 4h back → earliest = 05:00

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:00:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    REQUIRE_EQ(windows.size(), 1U);
    CHECK_EQ(windows[0].source, "leading_backfill");
    CHECK(windows[0].clampedToHistoryLimit);
    CHECK_EQ(windows[0].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T05:00:00Z"));
}

TEST_CASE("switchbot history backend drops gap entirely before history limit") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";
    sensor.firstEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T00:00:00Z");
    sensor.latestEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:00:00Z");

    switchbot::history::BackendSyncInterval old_gap;
    old_gap.startEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T01:00:00Z");
    old_gap.endEpoch   = *switchbot::history::parseIsoUtcEpoch("2026-05-08T02:00:00Z");
    sensor.syncIntervals = {old_gap};

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 6 * 60 * 60;
    options.historyLimitSeconds = 4 * 60 * 60;  // earliest = 05:00 — gap ends at 02:00

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:00:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    for (const auto& w : windows) {
        CHECK_NE(w.source, "internal_gap");
    }
}

TEST_CASE("switchbot history backend plans new sensor default window") {
    switchbot::history::BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-uuid";

    switchbot::history::HistoryPlanningOptions options;
    options.sampleIntervalSeconds = 15 * 60;
    options.newSensorWindowSeconds = 6 * 60 * 60;
    options.historyLimitSeconds = 68U * 24U * 60U * 60U;

    const std::uint32_t now = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:20:00Z");
    const auto windows = switchbot::history::planHistoryWindows(sensor, now, options);

    REQUIRE_EQ(windows.size(), 1U);
    CHECK_EQ(windows[0].source, "new_sensor");
    CHECK_EQ(windows[0].firstPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T09:30:00Z"));
    CHECK_EQ(windows[0].lastPointEpoch, *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:15:00Z"));
    CHECK_EQ(windows[0].pointCount, 24U);
}

TEST_CASE("switchbot history backend builds bulk upload payload") {
    std::vector<switchbot::history::BulkHistoryReading> readings;
    switchbot::history::BulkHistoryReading reading;
    reading.timestampEpoch = *switchbot::history::parseIsoUtcEpoch("2026-05-08T15:00:00Z");
    reading.temperatureC = 20.3;
    reading.humidityPct = 43;
    readings.push_back(reading);

    const std::string payload = switchbot::history::makeBulkUploadPayload("sensor-uuid", readings);
    CHECK(payload.find("\"sensor_id\":\"sensor-uuid\"") != std::string::npos);
    CHECK(payload.find("\"timestamp\":\"2026-05-08T15:00:00Z\"") != std::string::npos);
    CHECK(payload.find("\"temperature_c\":20.3") != std::string::npos);
    CHECK(payload.find("\"humidity_pct\":43") != std::string::npos);
}

TEST_CASE("switchbot history backend parses bulk upload row errors") {
    const std::string body = R"json({
        "errors": [
            {"index": 2, "code": "conflict", "message": "existing row preserved"}
        ]
    })json";

    const auto parsed = switchbot::history::parseBulkUploadResponse(body, 200);
    REQUIRE(parsed.ok);
    REQUIRE_EQ(parsed.errors.size(), 1U);
    CHECK_EQ(parsed.errors[0].index, 2U);
    CHECK_EQ(parsed.errors[0].code, "conflict");
    CHECK_EQ(parsed.errors[0].message, "existing row preserved");
}

TEST_CASE("selectAlignedReadings returns empty for empty inputs") {
    const auto window = makeWindow(1000, 1000, 1);
    CHECK(switchbot::history::selectAlignedReadings({}, window, 900, 60).empty());
    CHECK(switchbot::history::selectAlignedReadings({makeSample(1000)}, makeWindow(0, 0, 0), 900, 60).empty());
}

TEST_CASE("selectAlignedReadings selects exact match") {
    const std::vector<switchbot::history::Sample> samples = {
        makeSample(900, 21.0, 55),
        makeSample(1800, 22.0, 56),
        makeSample(2700, 23.0, 57),
    };
    // window: one target at epoch 1800, device interval 60s → tolerance 30s
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(1800, 1800, 1), 900, 60);
    REQUIRE_EQ(out.size(), 1U);
    CHECK_EQ(out[0].timestampEpoch, 1800U);
    CHECK(out[0].temperatureC == doctest::Approx(22.0));
    CHECK_EQ(out[0].humidityPct, 56U);
}

TEST_CASE("selectAlignedReadings selects nearest within tolerance") {
    // target=1800, sample at 1820 — distance 20, tolerance 30 → within
    const std::vector<switchbot::history::Sample> samples = {makeSample(1820, 22.5, 60)};
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(1800, 1800, 1), 900, 60);
    REQUIRE_EQ(out.size(), 1U);
    CHECK_EQ(out[0].timestampEpoch, 1800U);
    CHECK(out[0].temperatureC == doctest::Approx(22.5));
}

TEST_CASE("selectAlignedReadings rejects sample outside tolerance") {
    // target=1800, sample at 1840 — distance 40, tolerance 30 → outside
    const std::vector<switchbot::history::Sample> samples = {makeSample(1840)};
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(1800, 1800, 1), 900, 60);
    CHECK(out.empty());
}

TEST_CASE("selectAlignedReadings selects closest of two candidates") {
    // target=1800, samples at 1775 (dist 25) and 1820 (dist 20) — picks 1820
    const std::vector<switchbot::history::Sample> samples = {
        makeSample(1775, 21.0, 50),
        makeSample(1820, 22.0, 55),
    };
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(1800, 1800, 1), 900, 60);
    REQUIRE_EQ(out.size(), 1U);
    CHECK(out[0].temperatureC == doctest::Approx(22.0));
}

TEST_CASE("selectAlignedReadings handles multiple targets") {
    const std::vector<switchbot::history::Sample> samples = {
        makeSample(900,  21.0, 51),
        makeSample(1800, 22.0, 52),
        makeSample(2700, 23.0, 53),
    };
    // three 15-min targets at 900, 1800, 2700; device interval 60s
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(900, 2700, 3), 900, 60);
    REQUIRE_EQ(out.size(), 3U);
    CHECK_EQ(out[0].timestampEpoch, 900U);
    CHECK_EQ(out[1].timestampEpoch, 1800U);
    CHECK_EQ(out[2].timestampEpoch, 2700U);
    CHECK(out[2].temperatureC == doctest::Approx(23.0));
}

TEST_CASE("selectAlignedReadings uses target epoch not sample epoch in output") {
    // sample is 10s early; output timestamp should be the target, not the sample
    const std::vector<switchbot::history::Sample> samples = {makeSample(1790, 20.0, 50)};
    const auto out = switchbot::history::selectAlignedReadings(samples, makeWindow(1800, 1800, 1), 900, 60);
    REQUIRE_EQ(out.size(), 1U);
    CHECK_EQ(out[0].timestampEpoch, 1800U);
}

TEST_CASE("switchbot history backend apiUrl appends path without trailing slash") {
    Config config;
    config.api.baseUrl = "https://example.test/api";
    CHECK_EQ(switchbot::history::apiUrl(config, "/switchbot/sensors"),
             "https://example.test/api/switchbot/sensors");
}

TEST_CASE("switchbot history backend apiUrl strips trailing slash before appending path") {
    Config config;
    config.api.baseUrl = "https://example.test/api/";
    CHECK_EQ(switchbot::history::apiUrl(config, "/switchbot/sensors"),
             "https://example.test/api/switchbot/sensors");
}

TEST_CASE("switchbot history backend postBulkUpload returns error for empty sensor id") {
    const Config config{};
    const auto result = switchbot::history::postBulkUpload(config, "", {});
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("switchbot history backend postBulkUpload returns ok for empty readings") {
    const Config config{};
    const auto result = switchbot::history::postBulkUpload(config, "sensor-uuid", {});
    CHECK(result.ok);
}
