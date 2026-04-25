#include "salah/state.h"

#include "doctest/doctest.h"

namespace {

using salah::Phase;
using salah::Schedule;
using salah::State;

constexpr Schedule kToday{
    300,  // 05:00 fajr
    390,  // 06:30 sunrise
    600,  // 10:00 duha
    660,  // 11:00 dahwaEKubra
    720,  // 12:00 zuhr
    930,  // 15:30 asr
    1050, // 17:30 asr makruh
    1140, // 19:00 maghrib
    1230  // 20:30 isha
};

constexpr Schedule kTomorrow{
    301,  // 05:01 fajr
    391,  // 06:31 sunrise
    601,  // 10:01 duha
    661,  // 11:01 dahwaEKubra
    721,  // 12:01 zuhr
    931,  // 15:31 asr
    1051, // 17:31 asr makruh
    1141, // 19:01 maghrib
    1231  // 20:31 isha
};

struct StateCase {
    const char* name;
    int nowMinutes;
    Phase current;
    Phase next;
    int remainingMinutes;
};

void checkState(const State& state, const StateCase& expected) {
    INFO(expected.name);
    CHECK_EQ(state.current, expected.current);
    CHECK_EQ(state.next, expected.next);
    CHECK_EQ(state.minutesRemaining, expected.remainingMinutes);
}

} // namespace

TEST_CASE("salah state advances through each phase boundary") {
    const StateCase cases[] = {
        {"one minute before fajr", 299, Phase::Isha, Phase::Fajr, 1},
        {"exactly at fajr", 300, Phase::Fajr, Phase::SunriseMakruh, 90},
        {"between fajr and sunrise", 345, Phase::Fajr, Phase::SunriseMakruh, 45},
        {"one minute before sunrise", 389, Phase::Fajr, Phase::SunriseMakruh, 1},
        {"exactly at sunrise", 390, Phase::SunriseMakruh, Phase::Duha, 210},
        {"between sunrise and duha", 500, Phase::SunriseMakruh, Phase::Duha, 100},
        {"exactly at duha", 600, Phase::Duha, Phase::DahwaEKubra, 60},
        {"between duha and dahwa e kubra", 630, Phase::Duha, Phase::DahwaEKubra, 30},
        {"exactly at dahwa e kubra", 660, Phase::DahwaEKubra, Phase::Zuhr, 60},
        {"one minute before zuhr", 719, Phase::DahwaEKubra, Phase::Zuhr, 1},
        {"exactly at zuhr", 720, Phase::Zuhr, Phase::Asr, 210},
        {"between zuhr and asr", 800, Phase::Zuhr, Phase::Asr, 130},
        {"exactly at asr", 930, Phase::Asr, Phase::AsrMakruh, 210},
        {"between asr and asr makruh", 990, Phase::Asr, Phase::AsrMakruh, 150},
        {"exactly at asr makruh", 1050, Phase::AsrMakruh, Phase::Maghrib, 90},
        {"between asr makruh and maghrib", 1100, Phase::AsrMakruh, Phase::Maghrib, 40},
        {"exactly at maghrib", 1140, Phase::Maghrib, Phase::Isha, 90},
        {"between maghrib and isha", 1200, Phase::Maghrib, Phase::Isha, 30},
        {"exactly at isha", 1230, Phase::Isha, Phase::Fajr, 511},
        {"after isha", 1300, Phase::Isha, Phase::Fajr, 441},
    };

    for (const StateCase& expected : cases) {
        SUBCASE(expected.name) {
            const State state = salah::computeState(kToday, kTomorrow, expected.nowMinutes);
            checkState(state, expected);
        }
    }
}

TEST_CASE("salah state rolls over to tomorrow's fajr after isha") {
    const StateCase cases[] = {
        {"one minute before midnight", 1439, Phase::Isha, Phase::Fajr, 302},
        {"midnight", 0, Phase::Isha, Phase::Fajr, 300},
        {"last valid minute before midnight", 1438, Phase::Isha, Phase::Fajr, 303},
    };

    for (const StateCase& expected : cases) {
        SUBCASE(expected.name) {
            const State state = salah::computeState(kToday, kTomorrow, expected.nowMinutes);
            checkState(state, expected);
        }
    }
}
