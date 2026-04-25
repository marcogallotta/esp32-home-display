#include "config.h"
#include "helpers.h"
#include "salah/service.h"

#include "doctest/doctest.h"
#include "PrayerTimes.h"

namespace {

using salah::Schedule;

Config makeParisConfig(const std::string& dstRule) {
    Config config;

    config.forecast.openmeteoPem = "";
    config.forecast.updateIntervalMinutes = 60;

    config.location.latitude = 48.9f;
    config.location.longitude = 2.3f;
    config.location.timezone = "Europe/Paris";
    config.location.timezoneLong = "CET-1CEST,M3.5.0/2,M10.5.0/3";

    config.salah.timezoneOffsetMinutes = 60;
    config.salah.dstRule = dstRule;
    config.salah.asrMakruhMinutes = 20;
    config.salah.hanafiAsr = true;

    config.wifi.ssid = "";
    config.wifi.password = "";

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
    assertTrue(result.valid, "direct PrayerTimes result should be valid");

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

Schedule buildServiceScheduleOrFail(int day, int month, int year, const Config& config) {
    Schedule out{};
    const bool ok = salah::buildSchedule(day, month, year, config, out);
    assertTrue(ok, "buildSchedule should succeed");
    return out;
}

void assertScheduleEqual(const Schedule& actual, const Schedule& expected, const char* context) {
    assertEqual(actual.fajr, expected.fajr, std::string(context) + ": fajr mismatch");
    assertEqual(actual.sunrise, expected.sunrise, std::string(context) + ": sunrise mismatch");
    assertEqual(actual.duha, expected.duha, std::string(context) + ": duha mismatch");
    assertEqual(actual.dahwaEKubra, expected.dahwaEKubra, std::string(context) + ": dahwaEKubra mismatch");
    assertEqual(actual.zuhr, expected.zuhr, std::string(context) + ": zuhr mismatch");
    assertEqual(actual.asr, expected.asr, std::string(context) + ": asr mismatch");
    assertEqual(actual.asrMakruh, expected.asrMakruh, std::string(context) + ": asrMakruh mismatch");
    assertEqual(actual.maghrib, expected.maghrib, std::string(context) + ": maghrib mismatch");
    assertEqual(actual.isha, expected.isha, std::string(context) + ": isha mismatch");
}

void assertStrictlyOrdered(const Schedule& s, const char* context) {
    assertTrue(s.fajr < s.sunrise, std::string(context) + ": expected fajr < sunrise");
    assertTrue(s.sunrise < s.duha, std::string(context) + ": expected sunrise < duha");
    assertTrue(s.duha < s.dahwaEKubra, std::string(context) + ": expected duha < dahwaEKubra");
    assertTrue(s.dahwaEKubra < s.zuhr, std::string(context) + ": expected dahwaEKubra < zuhr");
    assertTrue(s.zuhr < s.asr, std::string(context) + ": expected zuhr < asr");
    assertTrue(s.asr < s.asrMakruh, std::string(context) + ": expected asr < asrMakruh");
    assertTrue(s.asrMakruh < s.maghrib, std::string(context) + ": expected asrMakruh < maghrib");
    assertTrue(s.maghrib < s.isha, std::string(context) + ": expected maghrib < isha");
}

TEST_CASE("testBasicSummerScheduleIsOrdered") {
    const Config config = makeParisConfig("eu");
    const Schedule s = buildServiceScheduleOrFail(4, 4, 2026, config);
    assertStrictlyOrdered(s, "summer schedule");
}

TEST_CASE("testBasicWinterScheduleIsOrdered") {
    const Config config = makeParisConfig("eu");
    const Schedule s = buildServiceScheduleOrFail(15, 1, 2026, config);
    assertStrictlyOrdered(s, "winter schedule");
}

TEST_CASE("testServiceMatchesDirectWhenDstOffInWinter") {
    const Config config = makeParisConfig("eu");
    const Schedule actual = buildServiceScheduleOrFail(15, 1, 2026, config);
    const Schedule expected = buildDirectSchedule(15, 1, 2026, 0, config);
    assertScheduleEqual(actual, expected, "winter direct comparison");
}

TEST_CASE("testServiceMatchesDirectWhenDstOnInSummer") {
    const Config config = makeParisConfig("eu");
    const Schedule actual = buildServiceScheduleOrFail(4, 4, 2026, config);
    const Schedule expected = buildDirectSchedule(4, 4, 2026, 60, config);
    assertScheduleEqual(actual, expected, "summer direct comparison");
}

TEST_CASE("testDstStartsOnLastSundayOfMarch") {
    const Config config = makeParisConfig("eu");
    const Schedule before = buildServiceScheduleOrFail(28, 3, 2026, config);
    const Schedule start = buildServiceScheduleOrFail(29, 3, 2026, config);

    const int zuhrDelta = start.zuhr - before.zuhr;
    const int fajrDelta = start.fajr - before.fajr;

    assertInRange(zuhrDelta, 55, 65, "DST start should shift zuhr by about +60 minutes");
    assertInRange(fajrDelta, 54, 66, "DST start should shift fajr by about +60 minutes");
}

TEST_CASE("testDstEndsOnLastSundayOfOctober") {
    const Config config = makeParisConfig("eu");
    const Schedule before = buildServiceScheduleOrFail(24, 10, 2026, config);
    const Schedule end = buildServiceScheduleOrFail(25, 10, 2026, config);

    const int zuhrDelta = end.zuhr - before.zuhr;
    const int fajrDelta = end.fajr - before.fajr;

    assertInRange(zuhrDelta, -65, -55, "DST end should shift zuhr by about -60 minutes");
    assertInRange(fajrDelta, -66, -54, "DST end should shift fajr by about -60 minutes");
}

TEST_CASE("testNoDstConfigDoesNotApplyDstAtMarchBoundary") {
    const Config config = makeParisConfig("none");
    const Schedule actual = buildServiceScheduleOrFail(29, 3, 2026, config);
    const Schedule expected = buildDirectSchedule(29, 3, 2026, 0, config);
    assertScheduleEqual(actual, expected, "no DST config on March boundary");
}

TEST_CASE("testNoDstConfigDoesNotApplyDstAtOctoberBoundary") {
    const Config config = makeParisConfig("none");
    const Schedule actual = buildServiceScheduleOrFail(25, 10, 2026, config);
    const Schedule expected = buildDirectSchedule(25, 10, 2026, 0, config);
    assertScheduleEqual(actual, expected, "no DST config on October boundary");
}

TEST_CASE("testLeapYearDateBuilds") {
    const Config config = makeParisConfig("eu");
    const Schedule s = buildServiceScheduleOrFail(29, 2, 2028, config);
    assertStrictlyOrdered(s, "leap year schedule");
}

TEST_CASE("testInvalidMonthFails") {
    const Config config = makeParisConfig("eu");

    Schedule out{};
    const bool ok = salah::buildSchedule(1, 0, 2026, config, out);
    assertTrue(!ok, "invalid month should fail");
}

TEST_CASE("testAnotherRepresentativeDateMatchesDirect") {
    const Config config = makeParisConfig("eu");
    const Schedule actual = buildServiceScheduleOrFail(1, 11, 2026, config);
    const Schedule expected = buildDirectSchedule(1, 11, 2026, 0, config);
    assertScheduleEqual(actual, expected, "post-DST direct comparison");
}

} // namespace
