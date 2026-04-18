#include "../config.h"
#include "service.h"
#include "types.h"

#include <array>
#include <ctime>
#include <stdexcept>
#include <tuple>

#include <PrayerTimes.h>

namespace salah {

namespace {

enum class Month {
    January = 0,
    February,
    March,
    April,
    May,
    June,
    July,
    August,
    September,
    October,
    November,
    December
};

enum class Weekday {
    Monday = 0,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    Sunday
};

Month monthFromOneBased(int month) {
    if (month < 1 || month > 12) {
        throw std::invalid_argument("month must be in range 1..12");
    }

    return static_cast<Month>(month - 1);
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(Month month, int year) {
    static constexpr std::array<int, 12> kDaysInMonth = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    const int monthIndex = static_cast<int>(month);
    if (monthIndex < 0 || monthIndex >= static_cast<int>(kDaysInMonth.size())) {
        throw std::invalid_argument("invalid month");
    }

    if (month == Month::February && isLeapYear(year)) {
        return 29;
    }

    return kDaysInMonth[monthIndex];
}

// Returns weekday for Gregorian calendar date.
// Uses Sakamoto's algorithm.
// Output is normalized to Monday=0, ..., Sunday=6.
Weekday dayOfWeek(int day, Month month, int year) {
    static constexpr std::array<int, 12> t = {
        0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
    };

    const int monthIndex = static_cast<int>(month);
    if (monthIndex < 0 || monthIndex >= static_cast<int>(t.size())) {
        throw std::invalid_argument("invalid month");
    }

    int adjustedYear = year;
    if (monthIndex < static_cast<int>(Month::March)) {
        adjustedYear -= 1;
    }

    const int sundayBased =
        (adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 +
         t[monthIndex] + day) % 7;
    // Sakamoto: 0=Sunday, 1=Monday, ..., 6=Saturday
    // Convert to Monday=0, ..., Sunday=6.
    const int mondayBased = (sundayBased + 6) % 7;

    return static_cast<Weekday>(mondayBased);
}

int lastSundayOfMonth(Month month, int year) {
    const int lastDay = daysInMonth(month, year);
    const Weekday weekday = dayOfWeek(lastDay, month, year);

    return lastDay - ((static_cast<int>(weekday) + 1) % 7);
}

// EU DST rule used by this project only:
// - starts on the last Sunday of March
// - ends on the last Sunday of October
//
// Date granularity only:
// - whole start day counts as DST
// - whole end day counts as non-DST
bool isEuDstDate(int day, Month month, int year) {
    const int monthIndex = static_cast<int>(month);

    if (monthIndex < static_cast<int>(Month::March) ||
        monthIndex > static_cast<int>(Month::October)) {
        return false;
    }

    if (monthIndex > static_cast<int>(Month::March) &&
        monthIndex < static_cast<int>(Month::October)) {
        return true;
    }

    if (month == Month::March) {
        return day >= lastSundayOfMonth(Month::March, year);
    }

    return day < lastSundayOfMonth(Month::October, year);
}

int computeDstMinutes(int day, Month month, int year, bool dstEu) {
    if (!dstEu) {
        return 0;
    }

    return isEuDstDate(day, month, year) ? 60 : 0;
}

int toMinutes(float value) {
    int hour = 0;
    int minute = 0;
    PrayerTimes::minutesToTime(value, hour, minute);
    return hour * 60 + minute;
}

Schedule toSchedule(const PrayerTimesResult& result, const Config& config) {
    const int fajrMin = toMinutes(result.fajr);
    const int sunriseMin = toMinutes(result.sunrise);
    const int duhaMin = toMinutes(result.duha);
    const int dahwaEKubraMin = toMinutes(result.dahwaEKubra);
    const int zuhrMin = toMinutes(result.dhuhr);
    const int asrMin = toMinutes(result.asr);
    const int asrMakruhMin = toMinutes(result.maghrib - config.salah.asrMakruhMinutes);
    const int maghribMin = toMinutes(result.maghrib);
    const int ishaMin = toMinutes(result.isha);

    return Schedule{
        fajrMin,
        sunriseMin,
        duhaMin,
        dahwaEKubraMin,
        zuhrMin,
        asrMin,
        asrMakruhMin,
        maghribMin,
        ishaMin,
    };
}

} // namespace

Schedule buildSchedule(int day, int month, int year, const Config& config) {
    const Month monthEnum = monthFromOneBased(month);

    PrayerTimes pt(config.location.latitude, config.location.longitude, config.salah.timezoneOffsetMinutes);
    pt.setCalculationMethod(CalculationMethods::KARACHI);
    pt.setAsrMethod(config.salah.hanafiAsr ? HANAFI : SHAFII);

    const int dstMinutes = computeDstMinutes(day, monthEnum, year, config.salah.dstRule == "eu");
    const PrayerTimesResult result = pt.calculateWithOffset(day, month, year, dstMinutes);

    return toSchedule(result, config);
}

std::tuple<Schedule, Schedule> computeSchedules(std::tm time, const Config &config) {
    const Schedule today =
        buildSchedule(
            time.tm_mday,
            time.tm_mon + 1,
            time.tm_year + 1900,
            config
        );
    
    std::tm tomorrowTime = time;
    tomorrowTime.tm_mday += 1;
    std::mktime(&tomorrowTime);
    const Schedule tomorrow =
        buildSchedule(
            tomorrowTime.tm_mday,
            tomorrowTime.tm_mon + 1,
            tomorrowTime.tm_year + 1900,
            config
        );
    
    return {today, tomorrow};
}

const char* toString(Phase phase) {
    switch (phase) {
        case Phase::Fajr:           return "Fajr";
        case Phase::SunriseMakruh:  return "Sunrise Makruh";
        case Phase::Duha:           return "Duha";
        case Phase::DahwaEKubra:    return "Dahwa e Kubra";
        case Phase::Zuhr:           return "Zuhr";
        case Phase::Asr:            return "Asr";
        case Phase::AsrMakruh:      return "Asr Makruh";
        case Phase::Maghrib:        return "Maghrib";
        case Phase::Isha:           return "Isha";
    }

    return "Unknown";
}

const char* toShortString(Phase phase) {
    switch (phase) {
        case Phase::Fajr:           return "Fajr";
        case Phase::SunriseMakruh:  return "Sunrise";
        case Phase::Duha:           return "Duha";
        case Phase::DahwaEKubra:    return "Makruh";
        case Phase::Zuhr:           return "Zuhr";
        case Phase::Asr:            return "Asr";
        case Phase::AsrMakruh:      return "Asr Mak";
        case Phase::Maghrib:        return "Maghreb";
        case Phase::Isha:           return "Isha";
    }

    return "Unknown";
}

int minutesSinceMidnight(const std::tm& time) {
    return time.tm_hour * 60 + time.tm_min;
}

} // namespace salah
