#pragma once

#include <cstdint>

namespace time_utils {

// Returns UTC epoch seconds for the given broken-down UTC time.
// year: full year (e.g. 2024), mon: 1-12, mday: 1-31.
// Returns -1 if the result is before the Unix epoch.
std::int64_t utcBrokenDownToEpoch(int year, int mon, int mday, int hour, int min, int sec);

} // namespace time_utils
