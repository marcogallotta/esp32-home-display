#include "sensor_readings.h"

#include "doctest/doctest.h"

#include <cstdint>

namespace {

SwitchbotReading completeSwitchbot(float temperatureC = 21.2f, std::uint8_t humidityPct = 55) {
    SwitchbotReading reading;
    reading.temperatureC = temperatureC;
    reading.humidityPct = humidityPct;
    return reading;
}

TEST_CASE("switchbot reading reports whether it has any value") {
    SwitchbotReading reading;
    CHECK_FALSE(reading.hasAnyValue());

    reading.temperatureC = 21.0f;
    CHECK(reading.hasAnyValue());

    reading = SwitchbotReading{};
    reading.humidityPct = 55;
    CHECK(reading.hasAnyValue());

    reading = SwitchbotReading{};
    reading.lastSeenEpochS = 12345;
    CHECK(reading.hasAnyValue());
}

TEST_CASE("switchbot reading is complete only when temperature and humidity are present") {
    SwitchbotReading reading;
    CHECK_FALSE(reading.hasCompleteReading());

    reading.temperatureC = 21.0f;
    CHECK_FALSE(reading.hasCompleteReading());

    reading = SwitchbotReading{};
    reading.humidityPct = 55;
    CHECK_FALSE(reading.hasCompleteReading());

    reading.temperatureC = 21.0f;
    CHECK(reading.hasCompleteReading());
}

TEST_CASE("switchbot API equality uses exact temperature and humidity values") {
    CHECK(completeSwitchbot(21.2f, 55).equalsForApi(completeSwitchbot(21.2f, 55)));
    CHECK_FALSE(completeSwitchbot(21.2f, 55).equalsForApi(completeSwitchbot(21.3f, 55)));
    CHECK_FALSE(completeSwitchbot(21.2f, 55).equalsForApi(completeSwitchbot(21.2f, 56)));
}

TEST_CASE("switchbot display equality uses rounded temperature and humidity") {
    CHECK(completeSwitchbot(21.2f, 55).equalsForDisplay(completeSwitchbot(21.49f, 55)));
    CHECK_FALSE(completeSwitchbot(21.49f, 55).equalsForDisplay(completeSwitchbot(21.5f, 55)));
    CHECK_FALSE(completeSwitchbot(21.2f, 55).equalsForDisplay(completeSwitchbot(21.2f, 56)));
}

TEST_CASE("switchbot incomplete readings compare equal for display only when both are incomplete") {
    SwitchbotReading empty;

    SwitchbotReading humidityOnly;
    humidityOnly.humidityPct = 55;

    CHECK(empty.equalsForDisplay(humidityOnly));
    CHECK_FALSE(empty.equalsForDisplay(completeSwitchbot()));
    CHECK_FALSE(humidityOnly.equalsForDisplay(completeSwitchbot()));
}

} // namespace
