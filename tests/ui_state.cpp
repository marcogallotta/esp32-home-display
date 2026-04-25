#include "helpers.h"
#include "salah/types.h"
#include "state.h"
#include "ui/state.h"

#include "doctest/doctest.h"

namespace {

State makeBaseState() {
    State state;

    state.hasSalah = true;
    state.salah.current = salah::Phase::Zuhr;
    state.salah.next = salah::Phase::Asr;
    state.salah.minutesRemaining = 42;

    state.switchbotSensors = {
        SwitchbotSensorState{
            SensorIdentity{"AA:BB:CC:DD:EE:FF", "Living Room", "LR"},
            SwitchbotReading{}
        },
        SwitchbotSensorState{
            SensorIdentity{"11:22:33:44:55:66", "Bedroom", "BR"},
            SwitchbotReading{}
        }
    };

    state.switchbotSensors[0].reading.temperatureC = 21.2f;
    state.switchbotSensors[0].reading.humidityPct = 55;
    state.switchbotSensors[0].reading.lastSeenEpochS = 1000;
    state.switchbotSensors[0].reading.rssi = -60;

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

TEST_CASE("testDisplayTempRounds") {
    assertEqual(displayTemp(21.2f), 21, "21.2 should round to 21");
    assertEqual(displayTemp(21.5f), 22, "21.5 should round to 22");
    assertEqual(displayTemp(-1.5f), -2, "-1.5 should round to -2");
}

TEST_CASE("testIdenticalStatesProduceNoDirtyRegions") {
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

TEST_CASE("testSalahNameDirtyWhenCurrentChanges") {
    const State previous = makeBaseState();
    State current = previous;
    current.salah.current = salah::Phase::Asr;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.salahName, "salahName should be dirty");
    assertTrue(!dirty.minutes, "minutes should not be dirty");
}

TEST_CASE("testMinutesDirtyWhenRemainingChanges") {
    const State previous = makeBaseState();
    State current = previous;
    current.salah.minutesRemaining = 41;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.salahName, "salahName should not be dirty");
    assertTrue(dirty.minutes, "minutes should be dirty");
}

TEST_CASE("testForecastDirtyWhenDayChanges") {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.days[1].weatherCode = 80;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.forecast, "forecast should be dirty");
}

TEST_CASE("testSingleSensorRowDirtyWhenHumidityChanges") {
    State previous = makeBaseState();
    State current = previous;
    current.switchbotSensors[0].reading.humidityPct = 56;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
    assertTrue(!dirty.sensorRows[1], "sensor row 1 should not be dirty");
}

TEST_CASE("testSingleSensorRowDirtyWhenReadingAppears") {
    State previous = makeBaseState();
    State current = previous;
    current.switchbotSensors[1].reading.temperatureC = 19.0f;
    current.switchbotSensors[1].reading.humidityPct = 44;
    current.switchbotSensors[1].reading.lastSeenEpochS = 1234;
    current.switchbotSensors[1].reading.rssi = -70;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(!dirty.sensorRows[0], "sensor row 0 should not be dirty");
    assertTrue(dirty.sensorRows[1], "sensor row 1 should be dirty");
}

TEST_CASE("testSensorNotDirtyWhenTempRoundsSame") {
    State previous = makeBaseState();
    State current = previous;
    current.switchbotSensors[0].reading.temperatureC = 21.49f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(!dirty.sensorsAny, "sensorsAny should not be dirty");
    assertTrue(!dirty.sensorRows[0], "sensor row 0 should not be dirty");
}

TEST_CASE("testSensorDirtyWhenTempRoundsDifferently") {
    State previous = makeBaseState();
    State current = previous;
    current.switchbotSensors[0].reading.temperatureC = 21.5f;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
}

TEST_CASE("testSensorDirtyWhenShortNameChanges") {
    State previous = makeBaseState();
    State current = previous;
    current.switchbotSensors[0].identity.shortName = "K";

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    assertTrue(dirty.sensorsAny, "sensorsAny should be dirty");
    assertTrue(dirty.sensorRows[0], "sensor row 0 should be dirty");
}

} // namespace
