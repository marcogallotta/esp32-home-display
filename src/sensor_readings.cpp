#include "sensor_readings.h"

#include <cmath>

namespace {

std::optional<int> roundedTemp(const std::optional<float>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(*value));
}

} // namespace

bool SwitchbotReading::hasAnyValue() const {
    return temperatureC.has_value() ||
           humidityPct.has_value() ||
           lastSeenEpochS.has_value() ||
           rssi.has_value();
}

bool SwitchbotReading::hasCompleteReading() const {
    return temperatureC.has_value() && humidityPct.has_value();
}

bool SwitchbotReading::equalsForDisplay(const SwitchbotReading& other) const {
    if (hasCompleteReading() != other.hasCompleteReading()) {
        return false;
    }
    if (!hasCompleteReading()) {
        return true;
    }

    return roundedTemp(temperatureC) == roundedTemp(other.temperatureC) &&
           humidityPct == other.humidityPct;
}

bool SwitchbotReading::equalsForApi(const SwitchbotReading& other) const {
    return temperatureC == other.temperatureC &&
           humidityPct == other.humidityPct &&
           lastSeenEpochS == other.lastSeenEpochS;
}

bool XiaomiReading::hasAnyValue() const {
    return temperatureC.has_value() ||
           moisturePct.has_value() ||
           lux.has_value() ||
           conductivityUsCm.has_value() ||
           lastSeenEpochS.has_value() ||
           rssi.has_value();
}

bool XiaomiReading::equalsForDisplay(const XiaomiReading& other) const {
    return roundedTemp(temperatureC) == roundedTemp(other.temperatureC) &&
           moisturePct == other.moisturePct &&
           lux == other.lux &&
           conductivityUsCm == other.conductivityUsCm;
}

bool XiaomiReading::equalsForApi(const XiaomiReading& other) const {
    return temperatureC == other.temperatureC &&
           moisturePct == other.moisturePct &&
           lux == other.lux &&
           conductivityUsCm == other.conductivityUsCm &&
           lastSeenEpochS == other.lastSeenEpochS;
}
