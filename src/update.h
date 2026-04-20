#pragma once

#include <ctime>

#include "config.h"
#include "salah/types.h"
#include "state.h"
#include "switchbot/ble.h"
#include "xiaomi/ble.h"

void updateSalahState(
    const Config& config,
    const std::tm& localTime,
    int& oldDay,
    salah::Schedule& today,
    salah::Schedule& tomorrow,
    State& state
);

void updateSensorState(
    const Config& config,
    const std::time_t now,
    switchbot::Scanner& scanner,
    State& state
);

void updateXiaomiState(
    const Config& config,
    const std::time_t now,
    xiaomi::Scanner& scanner,
    State& state
);

bool updateForecastState(
    const Config& config,
    State& state
);
