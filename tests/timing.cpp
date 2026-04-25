#include "config.h"
#include "timing.h"

#include "doctest/doctest.h"

#include <ctime>

namespace {

Config makeConfig(int forecastUpdateIntervalMinutes, int xiaomiUpdateIntervalMinutes = 60) {
    Config config;
    config.forecast.updateIntervalMinutes = forecastUpdateIntervalMinutes;
    config.xiaomi.updateIntervalMinutes = xiaomiUpdateIntervalMinutes;
    return config;
}

} // namespace

TEST_CASE("all timing tasks are due before they have been scheduled") {
    const TimingState timing{};

    CHECK(isSalahDue(1000, timing));
    CHECK(areSensorsDue(1000, timing));
    CHECK(areXiaomiDue(1000, timing));
    CHECK(isForecastDue(1000, timing));
}

TEST_CASE("salah updates align to the next minute boundary") {
    SUBCASE("from the middle of a minute") {
        TimingState timing;
        markSalahUpdated(12 * 60 + 34, timing);

        CHECK_EQ(timing.nextSalahDueEpochS, static_cast<std::time_t>(13 * 60));
    }

    SUBCASE("from an exact minute boundary") {
        TimingState timing;
        markSalahUpdated(12 * 60, timing);

        CHECK_EQ(timing.nextSalahDueEpochS, static_cast<std::time_t>(13 * 60));
    }
}

TEST_CASE("sensor scan updates are scheduled one minute later") {
    TimingState timing;
    markSensorsUpdated(1000, timing);

    CHECK_EQ(timing.nextSensorsDueEpochS, static_cast<std::time_t>(1060));
}

TEST_CASE("xiaomi updates use the configured interval") {
    TimingState timing;
    const Config config = makeConfig(17, 45);

    markXiaomiUpdated(1000, config, timing);

    CHECK_EQ(timing.nextXiaomiDueEpochS, static_cast<std::time_t>(1000 + 45 * 60));
}

TEST_CASE("forecast success uses the configured interval") {
    TimingState timing;
    const Config config = makeConfig(17);

    markForecastUpdatedSuccess(1000, config, timing);

    CHECK_EQ(timing.nextForecastDueEpochS, static_cast<std::time_t>(1000 + 17 * 60));
}

TEST_CASE("forecast failure retries after five minutes") {
    TimingState timing;
    markForecastUpdatedFailure(1000, timing);

    CHECK_EQ(timing.nextForecastDueEpochS, static_cast<std::time_t>(1300));
}

TEST_CASE("tasks become due exactly at their scheduled time") {
    TimingState timing;
    timing.nextSalahDueEpochS = 100;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 250;
    timing.nextForecastDueEpochS = 300;

    SUBCASE("salah") {
        CHECK_FALSE(isSalahDue(99, timing));
        CHECK(isSalahDue(100, timing));
    }

    SUBCASE("sensors") {
        CHECK_FALSE(areSensorsDue(199, timing));
        CHECK(areSensorsDue(200, timing));
    }

    SUBCASE("xiaomi") {
        CHECK_FALSE(areXiaomiDue(249, timing));
        CHECK(areXiaomiDue(250, timing));
    }

    SUBCASE("forecast") {
        CHECK_FALSE(isForecastDue(299, timing));
        CHECK(isForecastDue(300, timing));
    }
}

TEST_CASE("earliest due time is the minimum scheduled task time") {
    TimingState timing;
    timing.nextSalahDueEpochS = 500;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 700;
    timing.nextForecastDueEpochS = 800;

    CHECK_EQ(earliestDueEpochS(timing), static_cast<std::time_t>(200));
}

TEST_CASE("sleep duration runs until the earliest due task") {
    TimingState timing;
    timing.nextSalahDueEpochS = 500;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 700;
    timing.nextForecastDueEpochS = 800;

    CHECK_EQ(computeSleepMs(150, timing), 50000);
}

TEST_CASE("sleep duration is zero when any task is overdue") {
    TimingState timing;
    timing.nextSalahDueEpochS = 100;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 250;
    timing.nextForecastDueEpochS = 300;

    CHECK_EQ(computeSleepMs(150, timing), 0);
}
