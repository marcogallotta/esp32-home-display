#pragma once

#include <cstdint>
#include <vector>

#include "../config.h"
#include "../sensor_readings.h"
#include "../state.h"
#include "buffer.h"

namespace api {

struct SwitchbotApiState {
    std::vector<SwitchbotReading> lastSent;
};

struct XiaomiBufferedState {
    bool active = false;
    XiaomiReading reading;
    std::int64_t openedAtEpochS = 0;
};

struct XiaomiApiState {
    std::vector<XiaomiReading> lastSent;
    std::vector<XiaomiBufferedState> pending;
};

struct State {
    SwitchbotApiState switchbot;
    XiaomiApiState xiaomi;
    BufferState buffer;
};

void initState(const ::State& appState, State& apiState);

bool shouldSendSwitchbot(
    const SensorWritePolicyConfig& policy,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
);

bool shouldSendSwitchbot(
    const Config& config,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
);

bool shouldSendXiaomi(
    const SensorWritePolicyConfig& policy,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomi(
    const Config& config,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiTemperature(
    const SensorWritePolicyConfig& policy,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiMoisture(
    const SensorWritePolicyConfig& policy,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiLux(
    const SensorWritePolicyConfig& policy,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiConductivity(
    const SensorWritePolicyConfig& policy,
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

} // namespace api
