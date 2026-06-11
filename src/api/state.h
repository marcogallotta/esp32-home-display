#pragma once

#include <cstdint>
#include <vector>

#include "../config.h"
#include "../sensor_readings.h"
#include "../state.h"

namespace api {

struct SwitchbotApiState {
    std::vector<SwitchbotReading> lastSent;
};

struct State {
    SwitchbotApiState switchbot;
};

void initState(const ::State& appState, State& apiState);

bool shouldSendSwitchbot(
    const SensorWritePolicyConfig& policy,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
);

bool shouldSendSwitchbot(
    const ::Config& config,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
);

} // namespace api
