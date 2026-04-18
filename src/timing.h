#pragma once

#include <ctime>

#include "config.h"

struct TimingState {
    std::time_t nextSalahDueEpochS = 0;
    std::time_t nextSensorsDueEpochS = 0;
    std::time_t nextForecastDueEpochS = 0;
};

bool isSalahDue(std::time_t now, const TimingState& timing);
bool areSensorsDue(std::time_t now, const TimingState& timing);
bool isForecastDue(std::time_t now, const TimingState& timing);

void markSalahUpdated(std::time_t now, TimingState& timing);
void markSensorsUpdated(std::time_t now, TimingState& timing);
void markForecastUpdatedSuccess(std::time_t now, const Config& config, TimingState& timing);
void markForecastUpdatedFailure(std::time_t now, TimingState& timing);

std::time_t earliestDueEpochS(const TimingState& timing);
int computeSleepMs(std::time_t now, const TimingState& timing);
