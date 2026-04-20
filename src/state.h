#pragma once

#include <vector>

#include "forecast/openmeteo.h"
#include "salah/state.h"
#include "sensor_readings.h"

struct SwitchbotSensorState {
    SensorIdentity identity;
    SwitchbotReading reading;
};

struct XiaomiSensorState {
    SensorIdentity identity;
    XiaomiReading reading;
};

struct State {
    bool hasSalah = false;
    salah::State salah;

    std::vector<SwitchbotSensorState> switchbotSensors;
    std::vector<XiaomiSensorState> xiaomiSensors;

    bool hasForecast = false;
    forecast::ForecastData forecast;
};
