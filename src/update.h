#pragma once

#include <ctime>

#include "config.h"
#include "salah/types.h"
#include "switchbot/ble.h"
#include "ui/state.h"
#include "xiaomi/ble.h"

void updateSalahState(
    const Config& config,
    const std::tm& localTime,
    int& oldDay,
    salah::Schedule& today,
    salah::Schedule& tomorrow,
    UiState& uiState
);

void updateSensorState(
    const Config& config,
    const std::time_t now,
    switchbot::Scanner& scanner,
    UiState& uiState
);

void updateXiaomiState(
    const Config& config,
    const std::time_t now,
    xiaomi::Scanner& scanner,
    UiState& uiState
);

bool updateForecastState(
    const Config& config,
    UiState& uiState
);
