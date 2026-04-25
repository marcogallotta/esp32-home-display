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

XiaomiReading xiaomiReading(float temperatureC = 18.2f,
                           std::uint8_t moisturePct = 42,
                           int lux = 1200,
                           int conductivityUsCm = 500,
                           std::int64_t lastSeenEpochS = 12345) {
    XiaomiReading reading;
    reading.temperatureC = temperatureC;
    reading.moisturePct = moisturePct;
    reading.lux = lux;
    reading.conductivityUsCm = conductivityUsCm;
    reading.lastSeenEpochS = lastSeenEpochS;
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

    reading = SwitchbotReading{};
    reading.rssi = -71;
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

TEST_CASE("xiaomi reading reports whether it has any value") {
    XiaomiReading reading;
    CHECK_FALSE(reading.hasAnyValue());

    reading.temperatureC = 18.0f;
    CHECK(reading.hasAnyValue());

    reading = XiaomiReading{};
    reading.moisturePct = 42;
    CHECK(reading.hasAnyValue());

    reading = XiaomiReading{};
    reading.lux = 1200;
    CHECK(reading.hasAnyValue());

    reading = XiaomiReading{};
    reading.conductivityUsCm = 500;
    CHECK(reading.hasAnyValue());

    reading = XiaomiReading{};
    reading.lastSeenEpochS = 12345;
    CHECK(reading.hasAnyValue());

    reading = XiaomiReading{};
    reading.rssi = -80;
    CHECK(reading.hasAnyValue());
}

TEST_CASE("xiaomi API equality uses exact values including last seen time") {
    CHECK(xiaomiReading().equalsForApi(xiaomiReading()));
    CHECK_FALSE(xiaomiReading(18.2f).equalsForApi(xiaomiReading(18.3f)));
    CHECK_FALSE(xiaomiReading(18.2f, 42).equalsForApi(xiaomiReading(18.2f, 43)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200).equalsForApi(xiaomiReading(18.2f, 42, 1201)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200, 500).equalsForApi(xiaomiReading(18.2f, 42, 1200, 501)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200, 500, 12345).equalsForApi(xiaomiReading(18.2f, 42, 1200, 500, 12346)));
}

TEST_CASE("xiaomi display equality uses rounded temperature and visible plant values") {
    CHECK(xiaomiReading(18.2f, 42, 1200, 500).equalsForDisplay(xiaomiReading(18.49f, 42, 1200, 500, 99999)));
    CHECK_FALSE(xiaomiReading(18.49f, 42, 1200, 500).equalsForDisplay(xiaomiReading(18.5f, 42, 1200, 500)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200, 500).equalsForDisplay(xiaomiReading(18.2f, 43, 1200, 500)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200, 500).equalsForDisplay(xiaomiReading(18.2f, 42, 1201, 500)));
    CHECK_FALSE(xiaomiReading(18.2f, 42, 1200, 500).equalsForDisplay(xiaomiReading(18.2f, 42, 1200, 501)));
}

} // namespace
