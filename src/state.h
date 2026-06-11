#pragma once

#include <vector>

#include "forecast/openmeteo.h"
#include "salah/state.h"
#include "sensor_readings.h"

struct SwitchbotSensorState {
    SensorIdentity identity;
    SwitchbotReading reading;
};

struct State {
    bool hasSalah = false;
    salah::State salah;

    std::vector<SwitchbotSensorState> switchbotSensors;

    bool hasForecast = false;
    forecast::ForecastData forecast;
};
