#include "time_utils.h"

namespace time_utils {

std::int64_t utcBrokenDownToEpoch(int year, int mon, int mday, int hour, int min, int sec) {
    // timegm() is absent from ESP-IDF libc; derive epoch via proleptic Gregorian day count.
    // Shift Jan/Feb into the previous year so the leap-day always falls at month-end.
    if (mon < 3) { year--; mon += 12; }
    const long days = 365L * year + year/4 - year/100 + year/400
                      + (153 * mon - 457) / 5 + mday - 719469;
    const std::int64_t epoch = days * 86400LL + hour * 3600LL + min * 60LL + sec;
    return epoch >= 0 ? epoch : -1;
}

} // namespace time_utils
