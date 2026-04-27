#include "api/state.h"
#include "config.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

constexpr float kComfortableRoomTempC = 21.0f;
constexpr std::uint8_t kComfortableRoomHumidityPct = 55;
constexpr std::uint8_t kHealthyPlantMoisturePct = 42;
constexpr int kNormalConductivityUsCm = 500;
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

int configuredLuxThreshold(const SensorWritePolicyConfig& policy, int baselineLux) {
    const int tenPercentOfBaseline = static_cast<int>(baselineLux * policy.luxDeltaFraction);
    return std::min(static_cast<int>(policy.luxDeltaCap), tenPercentOfBaseline);
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

class XiaomiReadingBuilder {
public:
    XiaomiReadingBuilder() {
        reading_.temperatureC = kComfortableRoomTempC;
        reading_.moisturePct = kHealthyPlantMoisturePct;
        reading_.lux = 5'000;
        reading_.conductivityUsCm = kNormalConductivityUsCm;
        reading_.lastSeenEpochS = kFirstSeenEpochS;
    }

    XiaomiReadingBuilder& withTemperatureC(float value) {
        reading_.temperatureC = value;
        return *this;
    }

    XiaomiReadingBuilder& withMoisturePct(std::uint8_t value) {
        reading_.moisturePct = value;
        return *this;
    }

    XiaomiReadingBuilder& withLux(int value) {
        reading_.lux = value;
        return *this;
    }

    XiaomiReadingBuilder& withConductivityUsCm(int value) {
        reading_.conductivityUsCm = value;
        return *this;
    }

    XiaomiReadingBuilder& seenAgainAfterSeconds(std::int64_t seconds) {
        reading_.lastSeenEpochS = kFirstSeenEpochS + seconds;
        return *this;
    }

    XiaomiReading build() const {
        return reading_;
    }

private:
    XiaomiReading reading_;
};

XiaomiReadingBuilder xiaomiReading() {
    return XiaomiReadingBuilder{};
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

TEST_CASE("xiaomi sends the first complete reading because nothing has been sent before") {
    const Config config = testConfig();
    const XiaomiReading nothingSentYet;
    const auto firstCompleteReading = xiaomiReading().build();

    CHECK(api::shouldSendXiaomi(config, firstCompleteReading, nothingSentYet));
}

TEST_CASE("xiaomi ignores temperature drift below the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto noise = xiaomiReading()
        .withTemperatureC(kComfortableRoomTempC + policy.temperatureDeltaC - kJustUnderFloatThreshold)
        .build();

    CHECK_FALSE(api::shouldSendXiaomi(config, noise, lastSent));
}

TEST_CASE("xiaomi writes when temperature reaches the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto meaningfulChange = xiaomiReading()
        .withTemperatureC(kComfortableRoomTempC + policy.temperatureDeltaC)
        .build();

    CHECK(api::shouldSendXiaomi(config, meaningfulChange, lastSent));
}

TEST_CASE("xiaomi ignores moisture drift below the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto noise = xiaomiReading()
        .withMoisturePct(static_cast<std::uint8_t>(kHealthyPlantMoisturePct + policy.moistureDeltaPct - 1))
        .build();

    CHECK_FALSE(api::shouldSendXiaomi(config, noise, lastSent));
}

TEST_CASE("xiaomi writes when moisture reaches the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto meaningfulChange = xiaomiReading()
        .withMoisturePct(static_cast<std::uint8_t>(kHealthyPlantMoisturePct + policy.moistureDeltaPct))
        .build();

    CHECK(api::shouldSendXiaomi(config, meaningfulChange, lastSent));
}

TEST_CASE("xiaomi lux uses ten percent in ordinary light because it is below the configured cap") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const int ordinaryLightLux = 5'000;
    const int threshold = configuredLuxThreshold(policy, ordinaryLightLux);
    const auto lastSent = xiaomiReading().withLux(ordinaryLightLux).build();

    CHECK(threshold == static_cast<int>(ordinaryLightLux * policy.luxDeltaFraction));
    CHECK(threshold < static_cast<int>(policy.luxDeltaCap));

    const auto justBelowThreshold = xiaomiReading().withLux(ordinaryLightLux + threshold - 1).build();
    const auto atThreshold = xiaomiReading().withLux(ordinaryLightLux + threshold).build();

    CHECK_FALSE(api::shouldSendXiaomi(config, justBelowThreshold, lastSent));
    CHECK(api::shouldSendXiaomi(config, atThreshold, lastSent));
}

TEST_CASE("xiaomi lux uses the configured cap in bright sun because ten percent would be too chatty") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const int brightSunLux = 80'000;
    const int threshold = configuredLuxThreshold(policy, brightSunLux);
    const auto lastSent = xiaomiReading().withLux(brightSunLux).build();

    CHECK(threshold == static_cast<int>(policy.luxDeltaCap));
    CHECK(threshold < static_cast<int>(brightSunLux * policy.luxDeltaFraction));

    const auto justBelowThreshold = xiaomiReading().withLux(brightSunLux + threshold - 1).build();
    const auto atThreshold = xiaomiReading().withLux(brightSunLux + threshold).build();

    CHECK_FALSE(api::shouldSendXiaomi(config, justBelowThreshold, lastSent));
    CHECK(api::shouldSendXiaomi(config, atThreshold, lastSent));
}

TEST_CASE("xiaomi ignores conductivity drift below the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto noise = xiaomiReading()
        .withConductivityUsCm(kNormalConductivityUsCm + static_cast<int>(policy.conductivityDeltaUsCm) - 1)
        .build();

    CHECK_FALSE(api::shouldSendXiaomi(config, noise, lastSent));
}

TEST_CASE("xiaomi writes when conductivity reaches the configured sensor-write delta") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto meaningfulChange = xiaomiReading()
        .withConductivityUsCm(kNormalConductivityUsCm + static_cast<int>(policy.conductivityDeltaUsCm))
        .build();

    CHECK(api::shouldSendXiaomi(config, meaningfulChange, lastSent));
}

TEST_CASE("xiaomi ignores timestamp-only scan noise before the configured heartbeat") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto sameReadingSeenAgain = xiaomiReading()
        .seenAgainAfterSeconds(minutesToSeconds(policy.heartbeatMinutes) - 1)
        .build();

    CHECK_FALSE(api::shouldSendXiaomi(config, sameReadingSeenAgain, lastSent));
}

TEST_CASE("xiaomi writes an unchanged reading when the configured heartbeat expires") {
    const Config config = testConfig();
    const auto& policy = sensorWritePolicy(config);
    const auto lastSent = xiaomiReading().build();

    const auto heartbeatReading = xiaomiReading()
        .seenAgainAfterSeconds(minutesToSeconds(policy.heartbeatMinutes))
        .build();

    CHECK(api::shouldSendXiaomi(config, heartbeatReading, lastSent));
}
