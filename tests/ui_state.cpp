#include "state.h"
#include "ui/state.h"

#include "doctest/doctest.h"
#include "salah/types.h"

#include <cstddef>

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

void checkNoSensorRowsDirty(const DirtyRegions& dirty) {
    for (std::size_t i = 0; i < dirty.sensorRows.size(); ++i) {
        CAPTURE(i);
        CHECK_FALSE(dirty.sensorRows[i]);
    }
}

TEST_CASE("display temperature rounds to nearest integer") {
    CHECK_EQ(displayTemp(21.2f), 21);
    CHECK_EQ(displayTemp(21.5f), 22);
    CHECK_EQ(displayTemp(-1.5f), -2);
}

TEST_CASE("identical states do not mark any region dirty") {
    const State previous = makeBaseState();
    const State current = previous;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    CHECK_FALSE(dirty.salahName);
    CHECK_FALSE(dirty.minutes);
    CHECK_FALSE(dirty.sensorsAny);
    CHECK_FALSE(dirty.forecast);
    CHECK_EQ(dirty.sensorRows.size(), std::size_t{2});
    checkNoSensorRowsDirty(dirty);
}

TEST_CASE("salah phase and minute changes dirty only their own regions") {
    SUBCASE("current salah phase changes") {
        const State previous = makeBaseState();
        State current = previous;
        current.salah.current = salah::Phase::Asr;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        CHECK(dirty.salahName);
        CHECK_FALSE(dirty.minutes);
    }

    SUBCASE("remaining minutes change") {
        const State previous = makeBaseState();
        State current = previous;
        current.salah.minutesRemaining = 41;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        CHECK_FALSE(dirty.salahName);
        CHECK(dirty.minutes);
    }
}

TEST_CASE("forecast changes dirty the forecast region") {
    State previous = makeBaseState();
    State current = previous;
    current.forecast.days[1].weatherCode = 80;

    const DirtyRegions dirty = computeDirtyRegions(previous, current);

    CHECK(dirty.forecast);
}

TEST_CASE("sensor reading changes dirty only affected sensor rows") {
    SUBCASE("humidity changes on first sensor") {
        State previous = makeBaseState();
        State current = previous;
        current.switchbotSensors[0].reading.humidityPct = 56;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        REQUIRE_EQ(dirty.sensorRows.size(), std::size_t{2});
        CHECK(dirty.sensorsAny);
        CHECK(dirty.sensorRows[0]);
        CHECK_FALSE(dirty.sensorRows[1]);
    }

    SUBCASE("reading appears on second sensor") {
        State previous = makeBaseState();
        State current = previous;
        current.switchbotSensors[1].reading.temperatureC = 19.0f;
        current.switchbotSensors[1].reading.humidityPct = 44;
        current.switchbotSensors[1].reading.lastSeenEpochS = 1234;
        current.switchbotSensors[1].reading.rssi = -70;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        REQUIRE_EQ(dirty.sensorRows.size(), std::size_t{2});
        CHECK(dirty.sensorsAny);
        CHECK_FALSE(dirty.sensorRows[0]);
        CHECK(dirty.sensorRows[1]);
    }

    SUBCASE("short name changes") {
        State previous = makeBaseState();
        State current = previous;
        current.switchbotSensors[0].identity.shortName = "K";

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        REQUIRE_EQ(dirty.sensorRows.size(), std::size_t{2});
        CHECK(dirty.sensorsAny);
        CHECK(dirty.sensorRows[0]);
        CHECK_FALSE(dirty.sensorRows[1]);
    }
}

TEST_CASE("sensor temperature changes only dirty rows when displayed value changes") {
    SUBCASE("same rounded temperature does not dirty row") {
        State previous = makeBaseState();
        State current = previous;
        current.switchbotSensors[0].reading.temperatureC = 21.49f;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        REQUIRE_EQ(dirty.sensorRows.size(), std::size_t{2});
        CHECK_FALSE(dirty.sensorsAny);
        CHECK_FALSE(dirty.sensorRows[0]);
    }

    SUBCASE("different rounded temperature dirties row") {
        State previous = makeBaseState();
        State current = previous;
        current.switchbotSensors[0].reading.temperatureC = 21.5f;

        const DirtyRegions dirty = computeDirtyRegions(previous, current);

        REQUIRE_EQ(dirty.sensorRows.size(), std::size_t{2});
        CHECK(dirty.sensorsAny);
        CHECK(dirty.sensorRows[0]);
        CHECK_FALSE(dirty.sensorRows[1]);
    }
}

} // namespace
