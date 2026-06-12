#include "update.h"

#include "doctest/doctest.h"
#include "time_utils.h"

#include <cstdint>
#include <string>

namespace {

Config makeConfig() {
    Config config;
    config.switchbot.sensors = {
        {"d5-3a-42-86-2c-63", "South", "S"},
        {"AA:BB:CC:DD:EE:FF", "West", "W"},
    };
    return config;
}

State makeStateWithPreviousReadings() {
    State state;
    state.switchbotSensors.resize(2);
    state.switchbotSensors[0].reading.temperatureC = 18.0f;
    state.switchbotSensors[0].reading.humidityPct = 40;
    state.switchbotSensors[0].reading.lastSeenEpochS = 1;
    state.switchbotSensors[1].reading.temperatureC = 19.0f;
    state.switchbotSensors[1].reading.humidityPct = 41;
    state.switchbotSensors[1].reading.lastSeenEpochS = 2;
    return state;
}

} // namespace

TEST_CASE("switchbot backend v2 response updates configured sensors") {
    const Config config = makeConfig();
    State state = makeStateWithPreviousReadings();

    const std::string body = R"json({
        "sensors": [
            {
                "mac": "D53A42862C63",
                "name": "South upstream",
                "recorded_at": "2026-06-12T06:30:00+00:00",
                "temperature_c": 22.2,
                "humidity_pct": 32.0,
                "stale": false
            }
        ],
        "retry_after_secs": 177
    })json";

    const auto result = applySwitchbotBackendResponse(config, body, 1000, state);

    CHECK(result.ok);
    CHECK_EQ(result.retryAfterSecs, 177);

    REQUIRE_EQ(state.switchbotSensors.size(), static_cast<std::size_t>(2));
    CHECK_EQ(state.switchbotSensors[0].identity.mac, "d5-3a-42-86-2c-63");
    CHECK_EQ(state.switchbotSensors[0].identity.name, "South");
    CHECK_EQ(state.switchbotSensors[0].identity.shortName, "S");
    REQUIRE(state.switchbotSensors[0].reading.temperatureC.has_value());
    CHECK(state.switchbotSensors[0].reading.temperatureC.value() == doctest::Approx(22.2f));
    REQUIRE(state.switchbotSensors[0].reading.humidityPct.has_value());
    CHECK_EQ(state.switchbotSensors[0].reading.humidityPct.value(), static_cast<std::uint8_t>(32));
    REQUIRE(state.switchbotSensors[0].reading.lastSeenEpochS.has_value());
    CHECK_EQ(
        state.switchbotSensors[0].reading.lastSeenEpochS.value(),
        time_utils::utcBrokenDownToEpoch(2026, 6, 12, 6, 30, 0)
    );

    CHECK_EQ(state.switchbotSensors[1].identity.mac, "AA:BB:CC:DD:EE:FF");
    CHECK_FALSE(state.switchbotSensors[1].reading.temperatureC.has_value());
    CHECK_FALSE(state.switchbotSensors[1].reading.humidityPct.has_value());
    CHECK_FALSE(state.switchbotSensors[1].reading.lastSeenEpochS.has_value());
}

TEST_CASE("switchbot backend v2 parser rejects malformed payload") {
    const Config config = makeConfig();
    State state = makeStateWithPreviousReadings();

    const auto result = applySwitchbotBackendResponse(config, R"json({"retry_after_secs":177})json", 1000, state);

    CHECK_FALSE(result.ok);
    CHECK_EQ(result.retryAfterSecs, 5 * 60);
    CHECK(state.switchbotSensors[0].reading.temperatureC.has_value());
    CHECK(state.switchbotSensors[1].reading.temperatureC.has_value());
}

TEST_CASE("switchbot backend v2 parser applies recorded_at timezone offsets") {
    const Config config = makeConfig();
    State state = makeStateWithPreviousReadings();

    const std::string body = R"json({
        "sensors": [
            {
                "mac": "D5:3A:42:86:2C:63",
                "recorded_at": "2026-06-12T08:30:00+02:00",
                "temperature_c": 22.2,
                "humidity_pct": 32.0
            },
            {
                "mac": "AA:BB:CC:DD:EE:FF",
                "recorded_at": "2026-06-12T01:00:00-05:30",
                "temperature_c": 19.1,
                "humidity_pct": 41.0
            }
        ],
        "retry_after_secs": 177
    })json";

    const auto result = applySwitchbotBackendResponse(config, body, 1000, state);

    REQUIRE(result.ok);
    REQUIRE(state.switchbotSensors[0].reading.lastSeenEpochS.has_value());
    CHECK_EQ(
        state.switchbotSensors[0].reading.lastSeenEpochS.value(),
        time_utils::utcBrokenDownToEpoch(2026, 6, 12, 6, 30, 0)
    );
    REQUIRE(state.switchbotSensors[1].reading.lastSeenEpochS.has_value());
    CHECK_EQ(
        state.switchbotSensors[1].reading.lastSeenEpochS.value(),
        time_utils::utcBrokenDownToEpoch(2026, 6, 12, 6, 30, 0)
    );
}
