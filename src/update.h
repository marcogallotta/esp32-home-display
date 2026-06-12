#pragma once

#include <ctime>

#include "config.h"
#include "salah/types.h"
#include "state.h"

void updateSalahState(
    const Config& config,
    const std::tm& localTime,
    int& oldDay,
    salah::Schedule& today,
    salah::Schedule& tomorrow,
    State& state
);

bool updateSwitchbotFromBackend(
    const Config& config,
    std::time_t now,
    State& state
);

bool updateForecastState(
    const Config& config,
    State& state
);
