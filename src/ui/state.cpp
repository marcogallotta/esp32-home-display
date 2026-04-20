#include "state.h"

#include <cmath>

namespace {

bool sameRenderedSwitchbotRow(const SwitchbotSensorState& a, const SwitchbotSensorState& b) {
    return a.identity.name == b.identity.name &&
           a.identity.shortName == b.identity.shortName &&
           a.reading.equalsForDisplay(b.reading);
}

} // namespace

int displayTemp(float x) {
    return std::lround(x);
}

DirtyRegions computeDirtyRegions(const State& previous, const State& current) {
    DirtyRegions dirty;
    dirty.sensorRows.assign(current.switchbotSensors.size(), false);

    if (previous.hasSalah != current.hasSalah) {
        dirty.salahName = true;
        dirty.minutes = true;
    } else if (current.hasSalah) {
        if (previous.salah.current != current.salah.current ||
            previous.salah.next != current.salah.next) {
            dirty.salahName = true;
        }

        if (previous.salah.minutesRemaining != current.salah.minutesRemaining) {
            dirty.minutes = true;
        }
    }

    if (previous.hasForecast != current.hasForecast) {
        dirty.forecast = true;
    } else if (current.hasForecast) {
        if (previous.forecast.count != current.forecast.count) {
            dirty.forecast = true;
        } else {
            for (int i = 0; i < current.forecast.count; ++i) {
                const auto& prevDay = previous.forecast.days[i];
                const auto& currDay = current.forecast.days[i];
                if (prevDay.date != currDay.date ||
                    prevDay.weatherCode != currDay.weatherCode ||
                    displayTemp(prevDay.tempMax) != displayTemp(currDay.tempMax) ||
                    displayTemp(prevDay.tempMin) != displayTemp(currDay.tempMin) ||
                    prevDay.rainProbMax != currDay.rainProbMax) {
                    dirty.forecast = true;
                    break;
                }
            }
        }
    }

    for (std::size_t i = 0; i < current.switchbotSensors.size(); ++i) {
        if (!sameRenderedSwitchbotRow(previous.switchbotSensors[i], current.switchbotSensors[i])) {
            dirty.sensorRows[i] = true;
            dirty.sensorsAny = true;
        }
    }

    return dirty;
}
