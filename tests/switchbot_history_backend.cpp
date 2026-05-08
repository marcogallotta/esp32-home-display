#include "switchbot/history_backend.h"

#include "doctest/doctest.h"

#include <string>
#include <vector>

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
    CHECK(payload.find("\"sensors\"") != std::string::npos);
    CHECK(payload.find("AA:BB:CC:DD:EE:FF") != std::string::npos);
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
