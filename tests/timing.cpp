#include "timing.h"

#include "config.h"
#include "helpers.h"

namespace {

Config makeConfig(int forecastUpdateIntervalMinutes, int xiaomiUpdateIntervalMinutes = 60) {
    Config config;
    config.forecast.openmeteoPem = "";
    config.forecast.updateIntervalMinutes = forecastUpdateIntervalMinutes;
    config.xiaomi.updateIntervalMinutes = xiaomiUpdateIntervalMinutes;
    return config;
}

void testAllTasksDueAtStartup() {
    const TimingState timing{};

    assertTrue(isSalahDue(1000, timing), "salah should be due at startup");
    assertTrue(areSensorsDue(1000, timing), "sensors should be due at startup");
    assertTrue(areXiaomiDue(1000, timing), "xiaomi should be due at startup");
    assertTrue(isForecastDue(1000, timing), "forecast should be due at startup");
}

void testMarkSalahUpdatedAlignsToNextMinuteBoundary() {
    TimingState timing;
    markSalahUpdated(12 * 60 + 34, timing); // 00:12:34

    assertEqual(
        timing.nextSalahDueEpochS,
        static_cast<std::time_t>(13 * 60),
        "salah should align to next minute boundary"
    );
}

void testMarkSalahUpdatedAtExactBoundaryMovesToNextMinute() {
    TimingState timing;
    markSalahUpdated(12 * 60, timing); // exact boundary

    assertEqual(
        timing.nextSalahDueEpochS,
        static_cast<std::time_t>(13 * 60),
        "salah should move to next minute even at exact boundary"
    );
}

void testMarkSensorsUpdatedUsesNowPlusSixtySeconds() {
    TimingState timing;
    markSensorsUpdated(1000, timing);

    assertEqual(
        timing.nextSensorsDueEpochS,
        static_cast<std::time_t>(1060),
        "sensors should be scheduled 60s later"
    );
}

void testMarkXiaomiUpdatedUsesConfiguredInterval() {
    TimingState timing;
    const Config config = makeConfig(17, 45);

    markXiaomiUpdated(1000, config, timing);

    assertEqual(
        timing.nextXiaomiDueEpochS,
        static_cast<std::time_t>(1000 + 45 * 60),
        "xiaomi should use configured interval"
    );
}

void testMarkForecastUpdatedSuccessUsesConfiguredInterval() {
    TimingState timing;
    const Config config = makeConfig(17);

    markForecastUpdatedSuccess(1000, config, timing);

    assertEqual(
        timing.nextForecastDueEpochS,
        static_cast<std::time_t>(1000 + 17 * 60),
        "forecast success should use configured interval"
    );
}

void testMarkForecastUpdatedFailureUsesFiveMinutes() {
    TimingState timing;
    markForecastUpdatedFailure(1000, timing);

    assertEqual(
        timing.nextForecastDueEpochS,
        static_cast<std::time_t>(1300),
        "forecast failure should retry in 5 minutes"
    );
}

void testDueChecksWork() {
    TimingState timing;
    timing.nextSalahDueEpochS = 100;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 250;
    timing.nextForecastDueEpochS = 300;

    assertTrue(!isSalahDue(99, timing), "salah should not be due before due time");
    assertTrue(isSalahDue(100, timing), "salah should be due at due time");

    assertTrue(!areSensorsDue(199, timing), "sensors should not be due before due time");
    assertTrue(areSensorsDue(200, timing), "sensors should be due at due time");

    assertTrue(!areXiaomiDue(249, timing), "xiaomi should not be due before due time");
    assertTrue(areXiaomiDue(250, timing), "xiaomi should be due at due time");

    assertTrue(!isForecastDue(299, timing), "forecast should not be due before due time");
    assertTrue(isForecastDue(300, timing), "forecast should be due at due time");
}

void testEarliestDueEpochSReturnsMinimum() {
    TimingState timing;
    timing.nextSalahDueEpochS = 500;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 700;
    timing.nextForecastDueEpochS = 800;

    assertEqual(
        earliestDueEpochS(timing),
        static_cast<std::time_t>(200),
        "earliest due should be minimum due epoch"
    );
}

void testComputeSleepMsUsesEarliestDue() {
    TimingState timing;
    timing.nextSalahDueEpochS = 500;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 700;
    timing.nextForecastDueEpochS = 800;

    assertEqual(
        computeSleepMs(150, timing),
        50000,
        "sleep should be until earliest due"
    );
}

void testComputeSleepMsReturnsZeroWhenOverdue() {
    TimingState timing;
    timing.nextSalahDueEpochS = 100;
    timing.nextSensorsDueEpochS = 200;
    timing.nextXiaomiDueEpochS = 250;
    timing.nextForecastDueEpochS = 300;

    assertEqual(
        computeSleepMs(150, timing),
        0,
        "sleep should be zero when a task is already overdue"
    );
}

} // namespace

void runTimingTests() {
    testAllTasksDueAtStartup();
    testMarkSalahUpdatedAlignsToNextMinuteBoundary();
    testMarkSalahUpdatedAtExactBoundaryMovesToNextMinute();
    testMarkSensorsUpdatedUsesNowPlusSixtySeconds();
    testMarkXiaomiUpdatedUsesConfiguredInterval();
    testMarkForecastUpdatedSuccessUsesConfiguredInterval();
    testMarkForecastUpdatedFailureUsesFiveMinutes();
    testDueChecksWork();
    testEarliestDueEpochSReturnsMinimum();
    testComputeSleepMsUsesEarliestDue();
    testComputeSleepMsReturnsZeroWhenOverdue();
}
