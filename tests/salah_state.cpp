#include "helpers.h"
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

void assertState(const State& state,
                 Phase current,
                 Phase next,
                 int remaining,
                 const char* context) {
    assertEqual(state.current, current, std::string(context) + ": wrong current prayer");
    assertEqual(state.next, next, std::string(context) + ": wrong next prayer");
    assertEqual(state.minutesRemaining, remaining, std::string(context) + ": wrong remaining minutes");
}

TEST_CASE("testBeforeFajr") {
    const State state = salah::computeState(kToday, kTomorrow, 299);
    assertState(state, Phase::Isha, Phase::Fajr, 1, "1 minute before fajr");
}

TEST_CASE("testExactlyAtFajr") {
    const State state = salah::computeState(kToday, kTomorrow, 300);
    assertState(state, Phase::Fajr, Phase::SunriseMakruh, 90, "exactly at fajr");
}

TEST_CASE("testBetweenFajrAndSunrise") {
    const State state = salah::computeState(kToday, kTomorrow, 345);
    assertState(state, Phase::Fajr, Phase::SunriseMakruh, 45, "between fajr and sunrise");
}

TEST_CASE("testOneMinuteBeforeSunrise") {
    const State state = salah::computeState(kToday, kTomorrow, 389);
    assertState(state, Phase::Fajr, Phase::SunriseMakruh, 1, "1 minute before sunrise");
}

TEST_CASE("testExactlyAtSunrise") {
    const State state = salah::computeState(kToday, kTomorrow, 390);
    assertState(state, Phase::SunriseMakruh, Phase::Duha, 210, "exactly at sunrise");
}

TEST_CASE("testBetweenSunriseAndZuhr") {
    const State state = salah::computeState(kToday, kTomorrow, 500);
    assertState(state, Phase::SunriseMakruh, Phase::Duha, 100, "between sunrise and duha");
}

TEST_CASE("testExactlyAtDuha") {
    const State state = salah::computeState(kToday, kTomorrow, 600);
    assertState(state, Phase::Duha, Phase::DahwaEKubra, 60, "exactly at duha");
}

TEST_CASE("testBetweenDuhaAndDahwaEKubra") {
    const State state = salah::computeState(kToday, kTomorrow, 630);
    assertState(state, Phase::Duha, Phase::DahwaEKubra, 30, "between duha and dahwa e kubra");
}

TEST_CASE("testExactlyAtDahwaEKubra") {
    const State state = salah::computeState(kToday, kTomorrow, 660);
    assertState(state, Phase::DahwaEKubra, Phase::Zuhr, 60, "exactly at dahwa e kubra");
}

TEST_CASE("testOneMinuteBeforeZuhr") {
    const State state = salah::computeState(kToday, kTomorrow, 719);
    assertState(state, Phase::DahwaEKubra, Phase::Zuhr, 1, "1 minute before zuhr");
}

TEST_CASE("testExactlyAtZuhr") {
    const State state = salah::computeState(kToday, kTomorrow, 720);
    assertState(state, Phase::Zuhr, Phase::Asr, 210, "exactly at zuhr");
}

TEST_CASE("testBetweenZuhrAndAsr") {
    const State state = salah::computeState(kToday, kTomorrow, 800);
    assertState(state, Phase::Zuhr, Phase::Asr, 130, "between zuhr and asr");
}

TEST_CASE("testExactlyAtAsr") {
    const State state = salah::computeState(kToday, kTomorrow, 930);
    assertState(state, Phase::Asr, Phase::AsrMakruh, 210, "exactly at asr");
}

TEST_CASE("testBetweenAsrAndAsrMakruh") {
    const State state = salah::computeState(kToday, kTomorrow, 990);
    assertState(state, Phase::Asr, Phase::AsrMakruh, 150, "between asr and asr makruh");
}

TEST_CASE("testExactlyAtAsrMakruh") {
    const State state = salah::computeState(kToday, kTomorrow, 1050);
    assertState(state, Phase::AsrMakruh, Phase::Maghrib, 90, "exactly at asr makruh");
}

TEST_CASE("testBetweenAsrMakruhAndMaghrib") {
    const State state = salah::computeState(kToday, kTomorrow, 1100);
    assertState(state, Phase::AsrMakruh, Phase::Maghrib, 40, "between asr makruh and maghrib");
}

TEST_CASE("testExactlyAtMaghrib") {
    const State state = salah::computeState(kToday, kTomorrow, 1140);
    assertState(state, Phase::Maghrib, Phase::Isha, 90, "exactly at maghrib");
}

TEST_CASE("testBetweenMaghribAndIsha") {
    const State state = salah::computeState(kToday, kTomorrow, 1200);
    assertState(state, Phase::Maghrib, Phase::Isha, 30, "between maghrib and isha");
}

TEST_CASE("testExactlyAtIsha") {
    const State state = salah::computeState(kToday, kTomorrow, 1230);
    assertState(state, Phase::Isha, Phase::Fajr, 511, "exactly at isha");
}

TEST_CASE("testAfterIsha") {
    const State state = salah::computeState(kToday, kTomorrow, 1300);
    assertState(state, Phase::Isha, Phase::Fajr, 441, "after isha");
}

TEST_CASE("testOneMinuteBeforeMidnight") {
    const State state = salah::computeState(kToday, kTomorrow, 1439);
    assertState(state, Phase::Isha, Phase::Fajr, 302, "1 minute before midnight");
}

TEST_CASE("testAtMidnight") {
    const State state = salah::computeState(kToday, kTomorrow, 0);
    assertState(state, Phase::Isha, Phase::Fajr, 300, "midnight");
}

TEST_CASE("testAtLastMinuteOfDayWithinValidRange") {
    const State state = salah::computeState(kToday, kTomorrow, 1438);
    assertState(state, Phase::Isha, Phase::Fajr, 303, "late night rollover");
}

} // namespace
