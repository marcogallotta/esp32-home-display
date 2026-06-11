#include "api/state.h"
#include "config.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr float kComfortableRoomTempC = 21.0f;
constexpr std::uint8_t kComfortableRoomHumidityPct = 55;
constexpr std::int64_t kFirstSeenEpochS = 1'000;
constexpr float kJustUnderFloatThreshold = 0.01f;

Config testConfig() {
    return Config{};
}

const SensorWritePolicyConfig& sensorWritePolicy(const Config& config) {
    return config.api.sensorWritePolicy;
}

std::int64_t minutesToSeconds(int minutes) {
    return static_cast<std::int64_t>(minutes) * 60;
}

SwitchbotReading switchbotReading(float temperatureC = kComfortableRoomTempC,
                                  std::uint8_t humidityPct = kComfortableRoomHumidityPct,
                                  std::int64_t lastSeenEpochS = kFirstSeenEpochS) {
    SwitchbotReading reading;
    reading.temperatureC = temperatureC;
    reading.humidityPct = humidityPct;
    reading.lastSeenEpochS = lastSeenEpochS;
    return reading;
}

} // namespace

TEST_CASE("switchbot sends the first complete reading because nothing has been sent before") {
    const Config config = testConfig();
    const SwitchbotReading nothingSentYet;
    const auto firstCompleteReading = switchbotReading();

    CHECK(api::shouldSendSwitchbot(config, firstCompleteReading, nothingSentYet));
}

TEST_CASE("switchbot ignores temperature drift below the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto noise = switchbotReading(
        kComfortableRoomTempC + policy.temperatureDeltaC - kJustUnderFloatThreshold
    );

    CHECK_FALSE(api::shouldSendSwitchbot(config, noise, lastSent));
}

TEST_CASE("switchbot writes when temperature reaches the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto meaningfulChange = switchbotReading(
        kComfortableRoomTempC + policy.temperatureDeltaC
    );

    CHECK(api::shouldSendSwitchbot(config, meaningfulChange, lastSent));
}

TEST_CASE("switchbot ignores humidity drift below the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto noise = switchbotReading(
        kComfortableRoomTempC,
        static_cast<std::uint8_t>(kComfortableRoomHumidityPct + policy.humidityDeltaPct - 1)
    );

    CHECK_FALSE(api::shouldSendSwitchbot(config, noise, lastSent));
}

TEST_CASE("switchbot writes when humidity reaches the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto meaningfulChange = switchbotReading(
        kComfortableRoomTempC,
        static_cast<std::uint8_t>(kComfortableRoomHumidityPct + policy.humidityDeltaPct)
    );

    CHECK(api::shouldSendSwitchbot(config, meaningfulChange, lastSent));
}

TEST_CASE("switchbot ignores timestamp-only scan noise before the configured heartbeat") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto sameReadingSeenAgain = switchbotReading(
        kComfortableRoomTempC,
        kComfortableRoomHumidityPct,
        kFirstSeenEpochS + minutesToSeconds(policy.heartbeatMinutes) - 1
    );

    CHECK_FALSE(api::shouldSendSwitchbot(config, sameReadingSeenAgain, lastSent));
}

TEST_CASE("switchbot writes an unchanged reading when the configured heartbeat expires") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = switchbotReading();

    const auto heartbeatReading = switchbotReading(
        kComfortableRoomTempC,
        kComfortableRoomHumidityPct,
        kFirstSeenEpochS + minutesToSeconds(policy.heartbeatMinutes)
    );

    CHECK(api::shouldSendSwitchbot(config, heartbeatReading, lastSent));
}
