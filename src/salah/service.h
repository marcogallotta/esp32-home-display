#pragma once

#include <ctime>
#include <tuple>

#include "../config.h"
#include "types.h"

namespace salah {

// Builds prayer schedule (minutes since midnight) for a given date.
Schedule buildSchedule(int day, int month, int year,
                       const Config& config);

std::tuple<salah::Schedule, salah::Schedule> computeSchedules(std::tm time, const Config &config);

const char* toString(Phase phase);
const char* toShortString(Phase phase);

int minutesSinceMidnight(const std::tm& time);


} // namespace salah
