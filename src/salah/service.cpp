#include "../config.h"
#include "service.h"
#include "types.h"

#include <array>
#include <ctime>

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

bool monthFromOneBased(int month, Month& out) {
    if (month < 1 || month > 12) {
        return false;
    }

    out = static_cast<Month>(month - 1);
    return true;
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

bool daysInMonth(Month month, int year, int& out) {
    static constexpr std::array<int, 12> kDaysInMonth = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    const int monthIndex = static_cast<int>(month);
    if (monthIndex < 0 || monthIndex >= static_cast<int>(kDaysInMonth.size())) {
        return false;
    }

    out = kDaysInMonth[monthIndex];
    if (month == Month::February && isLeapYear(year)) {
        out = 29;
    }

    return true;
}

bool dayOfWeek(int day, Month month, int year, Weekday& out) {
    static constexpr std::array<int, 12> t = {
        0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
    };

    const int monthIndex = static_cast<int>(month);
    if (monthIndex < 0 || monthIndex >= static_cast<int>(t.size())) {
        return false;
    }

    int adjustedYear = year;
    if (monthIndex < static_cast<int>(Month::March)) {
        adjustedYear -= 1;
    }

    const int sundayBased =
        (adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 +
         t[monthIndex] + day) % 7;
    const int mondayBased = (sundayBased + 6) % 7;

    out = static_cast<Weekday>(mondayBased);
    return true;
}

bool lastSundayOfMonth(Month month, int year, int& out) {
    int lastDay = 0;
    if (!daysInMonth(month, year, lastDay)) {
        return false;
    }

    Weekday weekday;
    if (!dayOfWeek(lastDay, month, year, weekday)) {
        return false;
    }

    out = lastDay - ((static_cast<int>(weekday) + 1) % 7);
    return true;
}

bool isEuDstDate(int day, Month month, int year, bool& out) {
    const int monthIndex = static_cast<int>(month);

    if (monthIndex < static_cast<int>(Month::March) ||
        monthIndex > static_cast<int>(Month::October)) {
        out = false;
        return true;
    }

    if (monthIndex > static_cast<int>(Month::March) &&
        monthIndex < static_cast<int>(Month::October)) {
        out = true;
        return true;
    }

    int lastSunday = 0;
    if (month == Month::March) {
        if (!lastSundayOfMonth(Month::March, year, lastSunday)) {
            return false;
        }
        out = day >= lastSunday;
        return true;
    }

    if (!lastSundayOfMonth(Month::October, year, lastSunday)) {
        return false;
    }
    out = day < lastSunday;
    return true;
}

bool computeDstMinutes(int day, Month month, int year, bool dstEu, int& out) {
    if (!dstEu) {
        out = 0;
        return true;
    }

    bool isDst = false;
    if (!isEuDstDate(day, month, year, isDst)) {
        return false;
    }

    out = isDst ? 60 : 0;
    return true;
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

bool isValidDate(int day, int month, int year) {
    if (year < 1) {
        return false;
    }

    Month monthEnum;
    if (!monthFromOneBased(month, monthEnum)) {
        return false;
    }

    int maxDay = 0;
    if (!daysInMonth(monthEnum, year, maxDay)) {
        return false;
    }

    return day >= 1 && day <= maxDay;
}

} // namespace

bool buildSchedule(int day, int month, int year, const Config& config, Schedule& out) {
    if (!isValidDate(day, month, year)) {
        return false;
    }

    Month monthEnum;
    if (!monthFromOneBased(month, monthEnum)) {
        return false;
    }

    PrayerTimes pt(
        config.location.latitude,
        config.location.longitude,
        config.salah.timezoneOffsetMinutes
    );
    pt.setCalculationMethod(CalculationMethods::KARACHI);
    pt.setAsrMethod(config.salah.hanafiAsr ? HANAFI : SHAFII);

    const bool useEuDst = (config.salah.dstRule == "eu");
    int dstMinutes = 0;
    if (!computeDstMinutes(day, monthEnum, year, useEuDst, dstMinutes)) {
        return false;
    }

    const PrayerTimesResult result = pt.calculateWithOffset(day, month, year, dstMinutes);
    out = toSchedule(result, config);
    return true;
}

bool computeSchedules(std::tm time, const Config& config, Schedule& today, Schedule& tomorrow) {
    Schedule todayOut;
    if (!buildSchedule(
            time.tm_mday,
            time.tm_mon + 1,
            time.tm_year + 1900,
            config,
            todayOut
        )) {
        return false;
    }

    std::tm tomorrowTime = time;
    tomorrowTime.tm_mday += 1;
    if (std::mktime(&tomorrowTime) == static_cast<std::time_t>(-1)) {
        return false;
    }

    Schedule tomorrowOut;
    if (!buildSchedule(
            tomorrowTime.tm_mday,
            tomorrowTime.tm_mon + 1,
            tomorrowTime.tm_year + 1900,
            config,
            tomorrowOut
        )) {
        return false;
    }

    today = todayOut;
    tomorrow = tomorrowOut;
    return true;
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
