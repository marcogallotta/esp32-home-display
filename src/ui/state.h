#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../forecast/openmeteo.h"
#include "../salah/state.h"

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

struct UiState {
    bool hasSalah = false;
    salah::State salah;
    std::vector<SensorRowState> sensors;
    std::vector<XiaomiRowState> xiaomiSensors;
    bool hasForecast = false;
    forecast::ForecastData forecast;
};

struct DirtyRegions {
    bool salahName = false;
    bool minutes = false;
    bool sensorsAny = false;
    std::vector<bool> sensorRows;
    bool forecast = false;
};

int displayTemp(float x);
DirtyRegions computeDirtyRegions(const UiState& previous, const UiState& current);
