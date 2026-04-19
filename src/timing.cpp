#include "timing.h"

#include <algorithm>

namespace {

std::time_t nextMinuteBoundary(std::time_t now) {
    return ((now / 60) + 1) * 60;
}

} // namespace

bool isSalahDue(std::time_t now, const TimingState& timing) {
    return now >= timing.nextSalahDueEpochS;
}

bool areSensorsDue(std::time_t now, const TimingState& timing) {
    return now >= timing.nextSensorsDueEpochS;
}

bool areXiaomiDue(std::time_t now, const TimingState& timing) {
    return now >= timing.nextXiaomiDueEpochS;
}

bool isForecastDue(std::time_t now, const TimingState& timing) {
    return now >= timing.nextForecastDueEpochS;
}

void markSalahUpdated(std::time_t now, TimingState& timing) {
    timing.nextSalahDueEpochS = nextMinuteBoundary(now);
}

void markSensorsUpdated(std::time_t now, TimingState& timing) {
    timing.nextSensorsDueEpochS = now + 60;
}

void markXiaomiUpdated(std::time_t now, const Config& config, TimingState& timing) {
    timing.nextXiaomiDueEpochS =
        now + static_cast<std::time_t>(config.xiaomi.updateIntervalMinutes) * 60;
}

void markForecastUpdatedSuccess(std::time_t now, const Config& config, TimingState& timing) {
    timing.nextForecastDueEpochS =
        now + static_cast<std::time_t>(config.forecast.updateIntervalMinutes) * 60;
}

void markForecastUpdatedFailure(std::time_t now, TimingState& timing) {
    timing.nextForecastDueEpochS = now + 5 * 60;
}

std::time_t earliestDueEpochS(const TimingState& timing) {
    return std::min({
        timing.nextSalahDueEpochS,
        timing.nextSensorsDueEpochS,
        timing.nextXiaomiDueEpochS,
        timing.nextForecastDueEpochS,
    });
}

int computeSleepMs(std::time_t now, const TimingState& timing) {
    const std::time_t nextDue = earliestDueEpochS(timing);
    const std::time_t delayS = std::max<std::time_t>(0, nextDue - now);
    return static_cast<int>(delayS * 1000);
}
