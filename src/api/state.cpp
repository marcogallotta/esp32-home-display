#include "state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace {

template <typename T, typename ThresholdT>
bool deltaAtLeast(T a, T b, ThresholdT threshold) {
    if constexpr (std::is_floating_point_v<T> || std::is_floating_point_v<ThresholdT>) {
        constexpr double kEpsilon = 0.00001;
        const double delta = std::fabs(static_cast<double>(a) - static_cast<double>(b));
        return delta + kEpsilon >= static_cast<double>(threshold);
    } else {
        const auto lhs = static_cast<long long>(a);
        const auto rhs = static_cast<long long>(b);
        const auto delta = lhs > rhs ? lhs - rhs : rhs - lhs;
        return delta >= static_cast<long long>(threshold);
    }
}

template <typename T, typename ThresholdT>
bool optionalDeltaAtLeast(
    const std::optional<T>& current,
    const std::optional<T>& lastSent,
    ThresholdT threshold
) {
    return current.has_value() && lastSent.has_value() &&
           deltaAtLeast(*current, *lastSent, threshold);
}

bool heartbeatDue(
    const std::optional<std::int64_t>& current,
    const std::optional<std::int64_t>& lastSent,
    int heartbeatMinutes
) {
    return current.has_value() && lastSent.has_value() &&
           *current - *lastSent >= static_cast<std::int64_t>(heartbeatMinutes) * 60;
}

} // namespace

namespace api {

void initState(const ::State& appState, State& apiState) {
    apiState.switchbot.lastSent.assign(
        appState.switchbotSensors.size(),
        SwitchbotReading{}
    );
}

bool shouldSendSwitchbot(
    const SensorWritePolicyConfig& policy,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
) {
    if (!current.hasCompleteReading()) {
        return false;
    }

    if (!lastSent.hasCompleteReading()) {
        return true;
    }

    return optionalDeltaAtLeast(current.temperatureC, lastSent.temperatureC, policy.temperatureDeltaC) ||
           optionalDeltaAtLeast(current.humidityPct, lastSent.humidityPct, policy.humidityDeltaPct) ||
           heartbeatDue(current.lastSeenEpochS, lastSent.lastSeenEpochS, policy.heartbeatMinutes);
}

bool shouldSendSwitchbot(
    const ::Config& config,
    const SwitchbotReading& current,
    const SwitchbotReading& lastSent
) {
    return shouldSendSwitchbot(config.api.sensorWritePolicy, current, lastSent);
}

} // namespace api
