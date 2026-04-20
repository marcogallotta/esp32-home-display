#pragma once

#include <vector>

#include "../sensor_readings.h"
#include "../state.h"

namespace api {

struct SwitchbotApiState {
    std::vector<SwitchbotReading> lastSent;
};

struct XiaomiApiState {
    std::vector<XiaomiReading> lastSent;
};

struct State {
    SwitchbotApiState switchbot;
    XiaomiApiState xiaomi;
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
