#include "state.h"

namespace api {

void initState(const ::State& appState, State& apiState) {
    apiState.switchbot.lastSent.assign(
        appState.switchbotSensors.size(),
        SwitchbotReading{}
    );
    apiState.xiaomi.lastSent.assign(
        appState.xiaomiSensors.size(),
        XiaomiReading{}
    );
}

bool shouldSendSwitchbot(
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
) {
    if (!current.hasCompleteReading()) {
        return false;
    }
    return !current.equalsForApi(lastSent);
}

bool shouldSendXiaomiTemperature(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
) {
    return current.temperatureC.has_value() &&
           current.temperatureC != lastSent.temperatureC;
}

bool shouldSendXiaomiMoisture(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
) {
    return current.moisturePct.has_value() &&
           current.moisturePct != lastSent.moisturePct;
}

bool shouldSendXiaomiLux(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
) {
    return current.lux.has_value() &&
           current.lux != lastSent.lux;
}

bool shouldSendXiaomiConductivity(
    const XiaomiReading& current,
    const XiaomiReading& lastSent
) {
    return current.conductivityUsCm.has_value() &&
           current.conductivityUsCm != lastSent.conductivityUsCm;
}

} // namespace api
