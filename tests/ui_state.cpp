#include "state.h"
#include "ui/state.h"

#include "helpers.h"
#include "salah/types.h"

namespace {

State makeBaseState() {
    State state;

    state.hasSalah = true;
    state.salah.current = salah::Phase::Zuhr;
    state.salah.next = salah::Phase::Asr;
    state.salah.minutesRemaining = 42;

    state.sensors = {
        SensorRowState{
            true,
            'L',
            "Living Room",
            21.2f,
            55,
            1000,
            -60
        },
        SensorRowState{
            false,
            'B',
            "Bedroom",
            0.0f,
            0,
            0,
            0
        }
    };

    state.hasForecast = true;
    state.forecast.count = 2;
    state.forecast.days[0].date = "2026-04-18";
    state.forecast.days[0].weatherCode = 3;
    state.forecast.days[0].tempMax = 18.4f;
    state.forecast.days[0].tempMin = 9.2f;
    state.forecast.days[0].rainProbMax = 20;

    state.forecast.days[1].date = "2026-04-19";
    state.forecast.days[1].weatherCode = 61;
    state.forecast.days[1].tempMax = 16.1f;
    state.forecast.days[1].tempMin = 8.0f;
    state.forecast.days[1].rainProbMax = 70;

    return state;
}

void assertNoSensorRowsDirty(const DirtyRegions& dirty) {
    for (std::size_t i = 0; i < dirty.sensorRows.size(); ++i) {
        assertTrue(!dirty.sensorRows[i], "expected sensor row not dirty");
    }
}

void testDisplayTempRounds() {
    assertEqual(displayTemp(21.2f), 21, "21.2 should round to 21");
    assertEqual(displayTemp(21.5f), 22, "21.5 should round to 22");
    assertEqual(displayTemp(-1.5f), -2, "-1.5 should round to -2");
}

void testIdenticalStatesProduceNoDirtyRegions() {
    const State previous = makeBaseState();
    const State current = previous;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.salahName, "salahName should not be dirty");
    assertTrue(!dirty.minutes, "minutes should not be dirty");
    assertTrue(!dirty.sensorsAny, "sensorsAny should not be dirty");
    assertTrue(!dirty.forecast, "forecast should not be dirty");
    assertEqual(dirty.sensorRows.size(), std::size_t(2), "sensor row count should match");
    assertNoSensorRowsDirty(dirty);
}

void testSalahNameDirtyWhenCurrentChanges() {
    const State previous = makeBaseState();
    State current = previous;
    current.salah.current = salah::Phase::Asr;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.salahName, "salahName should be dirty");
    assertTrue(!dirty.minutes, "minutes should not be dirty");
}

void testSalahNameDirtyWhenNextChanges() {
    const State previous = makeBaseState();
    State current = previous;
    current.salah.next = salah::Phase::Maghrib;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.salahName, "salahName should be dirty");
    assertTrue(!dirty.minutes, "minutes should not be dirty");
}

void testMinutesDirtyWhenRemainingChanges() {
    const State previous = makeBaseState();
    State current = previous;
    current.salah.minutesRemaining = 41;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.salahName, "salahName should not be dirty");
    assertTrue(dirty.minutes, "minutes should be dirty");
}

void testSalahRegionsDirtyWhenHasSalahChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.hasSalah = false;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.salahName, "salahName should be dirty");
    assertTrue(dirty.minutes, "minutes should be dirty");
}

void testForecastDirtyWhenHasForecastChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.hasForecast = false;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.forecast, "forecast should be dirty");
}

void testForecastDirtyWhenCountChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.count = 1;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.forecast, "forecast should be dirty");
}

void testForecastDirtyWhenDayChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.days[1].weatherCode = 80;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.forecast, "forecast should be dirty");
}

void testForecastNotDirtyWhenTempsRoundSame() {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.days[0].tempMax = 18.49f;
    current.forecast.days[0].tempMin = 9.49f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.forecast, "forecast should not be dirty");
}

void testForecastDirtyWhenTempsRoundDifferently() {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.days[0].tempMax = 18.6f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.forecast, "forecast should be dirty");
}

void testSingleSensorRowDirtyWhenHumidityChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.sensors[0].humidity = 56;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
    assertTrue(!dirty.sensorRows[1], "sensor row 1 should not be dirty");
}

void testSingleSensorRowDirtyWhenReadingAppears() {
    State previous = makeBaseState();
    State current = previous;
    current.sensors[1].hasReading = true;
    current.sensors[1].temperatureC = 19.0f;
    current.sensors[1].humidity = 44;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(!dirty.sensorRows[0], "sensor row 0 should not be dirty");
    assertTrue(dirty.sensorRows[1], "sensor row 1 should be dirty");
}

void testSensorNotDirtyWhenTempRoundsSame() {
    State previous = makeBaseState();
    State current = previous;
    current.sensors[0].temperatureC = 21.49f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.sensorsAny, "sensorsAny should not be dirty");
    assertTrue(!dirty.sensorRows[0], "sensor row 0 should not be dirty");
}

void testSensorDirtyWhenTempRoundsDifferently() {
    State previous = makeBaseState();
    State current = previous;
    current.sensors[0].temperatureC = 21.5f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
}

void testSensorDirtyWhenShortNameChanges() {
    State previous = makeBaseState();
    State current = previous;
    current.sensors[0].shortName = 'K';

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
}

} // namespace

void runUiStateTests() {
    testDisplayTempRounds();
    testIdenticalStatesProduceNoDirtyRegions();
    testSalahNameDirtyWhenCurrentChanges();
    testSalahNameDirtyWhenNextChanges();
    testMinutesDirtyWhenRemainingChanges();
    testSalahRegionsDirtyWhenHasSalahChanges();
    testForecastDirtyWhenHasForecastChanges();
    testForecastDirtyWhenCountChanges();
    testForecastDirtyWhenDayChanges();
    testForecastNotDirtyWhenTempsRoundSame();
    testForecastDirtyWhenTempsRoundDifferently();
    testSingleSensorRowDirtyWhenHumidityChanges();
    testSingleSensorRowDirtyWhenReadingAppears();
    testSensorNotDirtyWhenTempRoundsSame();
    testSensorDirtyWhenTempRoundsDifferently();
    testSensorDirtyWhenShortNameChanges();
}
