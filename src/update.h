#pragma once

#include <ctime>
#include <string>

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

struct SwitchbotBackendUpdateResult {
    bool ok = false;
    int retryAfterSecs = 5 * 60;
};

SwitchbotBackendUpdateResult applySwitchbotBackendResponse(
    const Config& config,
    const std::string& body,
    std::time_t now,
    State& state
);

SwitchbotBackendUpdateResult updateSwitchbotFromBackend(
    const Config& config,
    std::time_t now,
    State& state
);

bool updateForecastState(
    const Config& config,
    State& state
);
