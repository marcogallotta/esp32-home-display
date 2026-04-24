#pragma once

#include <cstdint>
#include <vector>

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
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
);

bool shouldSendXiaomiTemperature(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiMoisture(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiLux(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

bool shouldSendXiaomiConductivity(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
);

} // namespace api
