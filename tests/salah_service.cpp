#include "config.h"
#include "salah/service.h"

#include "doctest/doctest.h"
#include "PrayerTimes.h"

#include <string>

namespace {

using salah::Schedule;

Config makeParisConfig(const std::string& dstRule = "eu", bool hanafiAsr = false) {
    Config config;

    config.location.latitude = 48.9f;
    config.location.longitude = 2.3f;
    config.location.timezone = "Europe/Paris";
    config.location.timezoneLong = "CET-1CEST,M3.5.0/2,M10.5.0/3";

    config.salah.timezoneOffsetMinutes = 60;
    config.salah.dstRule = dstRule;
    config.salah.asrMakruhMinutes = 20;
    config.salah.hanafiAsr = hanafiAsr;

    return config;
}

int toMinutes(float value) {
    int hour = 0;
    int minute = 0;
    PrayerTimes::minutesToTime(value, hour, minute);
    return hour * 60 + minute;
}

Schedule buildDirectSchedule(int day, int month, int year, int dstMinutes, const Config& config) {
    PrayerTimes pt(
        config.location.latitude,
        config.location.longitude,
        config.salah.timezoneOffsetMinutes
    );
    pt.setCalculationMethod(CalculationMethods::KARACHI);
    pt.setAsrMethod(config.salah.hanafiAsr ? HANAFI : SHAFII);

    const PrayerTimesResult result = pt.calculateWithOffset(day, month, year, dstMinutes);
    REQUIRE(result.valid);

    return Schedule{
        toMinutes(result.fajr),
        toMinutes(result.sunrise),
        toMinutes(result.duha),
        toMinutes(result.dahwaEKubra),
        toMinutes(result.dhuhr),
        toMinutes(result.asr),
        toMinutes(result.maghrib - config.salah.asrMakruhMinutes),
        toMinutes(result.maghrib),
        toMinutes(result.isha),
    };
}

Schedule buildServiceSchedule(int day, int month, int year, const Config& config) {
    Schedule out{};
    REQUIRE(salah::buildSchedule(day, month, year, config, out));
    return out;
}

void checkScheduleEquals(const Schedule& actual, const Schedule& expected) {
    CHECK_EQ(actual.fajr, expected.fajr);
    CHECK_EQ(actual.sunrise, expected.sunrise);
    CHECK_EQ(actual.duha, expected.duha);
    CHECK_EQ(actual.dahwaEKubra, expected.dahwaEKubra);
    CHECK_EQ(actual.zuhr, expected.zuhr);
    CHECK_EQ(actual.asr, expected.asr);
    CHECK_EQ(actual.asrMakruh, expected.asrMakruh);
    CHECK_EQ(actual.maghrib, expected.maghrib);
    CHECK_EQ(actual.isha, expected.isha);
}

void checkScheduleIsStrictlyOrdered(const Schedule& s) {
    CHECK_LT(s.fajr, s.sunrise);
    CHECK_LT(s.sunrise, s.duha);
    CHECK_LT(s.duha, s.dahwaEKubra);
    CHECK_LT(s.dahwaEKubra, s.zuhr);
    CHECK_LT(s.zuhr, s.asr);
    CHECK_LT(s.asr, s.asrMakruh);
    CHECK_LT(s.asrMakruh, s.maghrib);
    CHECK_LT(s.maghrib, s.isha);
}

void checkServiceMatchesDirect(int day, int month, int year, int expectedDstMinutes, const Config& config) {
    const Schedule actual = buildServiceSchedule(day, month, year, config);
    const Schedule expected = buildDirectSchedule(day, month, year, expectedDstMinutes, config);
    checkScheduleEquals(actual, expected);
}

} // namespace

TEST_CASE("salah service builds ordered schedules for representative dates") {
    SUBCASE("winter") {
        checkScheduleIsStrictlyOrdered(buildServiceSchedule(15, 1, 2026, makeParisConfig()));
    }

    SUBCASE("summer") {
        checkScheduleIsStrictlyOrdered(buildServiceSchedule(4, 4, 2026, makeParisConfig()));
    }

    SUBCASE("leap day") {
        checkScheduleIsStrictlyOrdered(buildServiceSchedule(29, 2, 2028, makeParisConfig()));
    }
}

TEST_CASE("salah service matches direct PrayerTimes calculation") {
    SUBCASE("winter has no DST offset") {
        checkServiceMatchesDirect(15, 1, 2026, 0, makeParisConfig("eu"));
    }

    SUBCASE("summer applies EU DST offset") {
        checkServiceMatchesDirect(4, 4, 2026, 60, makeParisConfig("eu"));
    }

    SUBCASE("after EU DST ends has no DST offset") {
        checkServiceMatchesDirect(1, 11, 2026, 0, makeParisConfig("eu"));
    }

    SUBCASE("dst_rule none never applies DST at March boundary") {
        checkServiceMatchesDirect(29, 3, 2026, 0, makeParisConfig("none"));
    }

    SUBCASE("dst_rule none never applies DST at October boundary") {
        checkServiceMatchesDirect(25, 10, 2026, 0, makeParisConfig("none"));
    }
}

TEST_CASE("salah service passes Asr method from config") {
    SUBCASE("Shafii Asr when hanafi_asr is false") {
        checkServiceMatchesDirect(4, 4, 2026, 60, makeParisConfig("eu", false));
    }

    SUBCASE("Hanafi Asr when hanafi_asr is true") {
        checkServiceMatchesDirect(4, 4, 2026, 60, makeParisConfig("eu", true));
    }

    SUBCASE("Hanafi Asr is later than Shafii Asr") {
        const Schedule shafii = buildServiceSchedule(4, 4, 2026, makeParisConfig("eu", false));
        const Schedule hanafi = buildServiceSchedule(4, 4, 2026, makeParisConfig("eu", true));

        CHECK_GT(hanafi.asr, shafii.asr);
        CHECK_GT(hanafi.asrMakruh, hanafi.asr);
    }
}

TEST_CASE("salah service applies EU DST on boundary days") {
    SUBCASE("last Sunday of March shifts schedule about one hour later") {
        const Schedule before = buildServiceSchedule(28, 3, 2026, makeParisConfig("eu"));
        const Schedule start = buildServiceSchedule(29, 3, 2026, makeParisConfig("eu"));

        CHECK_GE(start.zuhr - before.zuhr, 55);
        CHECK_LE(start.zuhr - before.zuhr, 65);
        CHECK_GE(start.fajr - before.fajr, 54);
        CHECK_LE(start.fajr - before.fajr, 66);
    }

    SUBCASE("last Sunday of October shifts schedule about one hour earlier") {
        const Schedule before = buildServiceSchedule(24, 10, 2026, makeParisConfig("eu"));
        const Schedule end = buildServiceSchedule(25, 10, 2026, makeParisConfig("eu"));

        CHECK_GE(end.zuhr - before.zuhr, -65);
        CHECK_LE(end.zuhr - before.zuhr, -55);
        CHECK_GE(end.fajr - before.fajr, -66);
        CHECK_LE(end.fajr - before.fajr, -54);
    }
}

TEST_CASE("salah service rejects invalid dates") {
    Config config = makeParisConfig();
    Schedule out{};

    CHECK_FALSE(salah::buildSchedule(1, 0, 2026, config, out));
}
