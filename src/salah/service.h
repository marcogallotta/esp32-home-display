#pragma once

#include <ctime>

#include "../config.h"
#include "types.h"

namespace salah {

bool buildSchedule(int day, int month, int year, const Config& config, Schedule& out);
bool computeSchedules(std::tm time, const Config& config, Schedule& today, Schedule& tomorrow);

const char* toString(Phase phase);
const char* toShortString(Phase phase);

int minutesSinceMidnight(const std::tm& time);

} // namespace salah