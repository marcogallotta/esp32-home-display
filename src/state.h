#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "forecast/openmeteo.h"
#include "salah/state.h"

struct SensorRowState {
    bool hasReading = false;
    char shortName = '?';
    std::string name;
    float temperatureC = 0.0f;
    std::uint8_t humidity = 0;
    std::int64_t lastSeenEpochS = 0;
    int rssi = 0;
};

struct XiaomiRowState {
    bool hasReading = false;
    char shortName = '?';
    std::string name;

    bool hasTemperature = false;
    float temperatureC = 0.0f;

    bool hasLux = false;
    int lux = 0;

    bool hasMoisture = false;
    std::uint8_t moisturePct = 0;

    bool hasConductivity = false;
    int conductivityUsCm = 0;

    std::int64_t lastSeenEpochS = 0;
    int rssi = 0;
};

struct State {
    bool hasSalah = false;
    salah::State salah;

    std::vector<SensorRowState> sensors;
    std::vector<XiaomiRowState> xiaomiSensors;

    bool hasForecast = false;
    forecast::ForecastData forecast;
};
