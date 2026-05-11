#include "forecast/openmeteo.h"

#include "config.h"
#include "doctest/doctest.h"

#include <string>

namespace {

const std::string kFourDayResponse = R"json({
    "daily": {
        "time": ["2026-05-11","2026-05-12","2026-05-13","2026-05-14"],
        "weather_code": [3, 61, 80, 1],
        "temperature_2m_max": [24.5, 19.0, 17.2, 22.8],
        "temperature_2m_min": [14.1, 12.3, 11.0, 15.5],
        "precipitation_probability_max": [10, 80, 65, 5],
        "precipitation_sum": [0.0, 5.2, 3.1, 0.0]
    }
})json";

LocationConfig location(float lat, float lon, const std::string& tz) {
    LocationConfig loc;
    loc.latitude = lat;
    loc.longitude = lon;
    loc.timezone = tz;
    loc.timezoneLong = tz;
    return loc;
}

} // namespace

TEST_CASE("forecast parseForecastJson parses all fields for 4-day response") {
    forecast::ForecastData out;
    REQUIRE(forecast::parseForecastJson(kFourDayResponse, out));
    CHECK_EQ(out.count, 4);

    CHECK_EQ(out.days[0].date, "2026-05-11");
    CHECK_EQ(out.days[0].weatherCode, 3);
    CHECK(out.days[0].tempMax == doctest::Approx(24.5f));
    CHECK(out.days[0].tempMin == doctest::Approx(14.1f));
    CHECK_EQ(out.days[0].rainProbMax, 10);

    CHECK_EQ(out.days[3].date, "2026-05-14");
    CHECK_EQ(out.days[3].weatherCode, 1);
    CHECK(out.days[3].tempMax == doctest::Approx(22.8f));
    CHECK(out.days[3].tempMin == doctest::Approx(15.5f));
    CHECK_EQ(out.days[3].rainProbMax, 5);
}

TEST_CASE("forecast parseForecastJson handles fewer than 4 days") {
    const std::string json = R"json({
        "daily": {
            "time": ["2026-05-11","2026-05-12"],
            "weather_code": [3, 61],
            "temperature_2m_max": [24.5, 19.0],
            "temperature_2m_min": [14.1, 12.3],
            "precipitation_probability_max": [10, 80],
            "precipitation_sum": [0.0, 5.2]
        }
    })json";

    forecast::ForecastData out;
    REQUIRE(forecast::parseForecastJson(json, out));
    CHECK_EQ(out.count, 2);
    CHECK_EQ(out.days[0].date, "2026-05-11");
    CHECK_EQ(out.days[1].date, "2026-05-12");
}

TEST_CASE("forecast parseForecastJson caps at 4 days when response has more") {
    const std::string json = R"json({
        "daily": {
            "time": ["2026-05-11","2026-05-12","2026-05-13","2026-05-14","2026-05-15","2026-05-16"],
            "weather_code": [3, 61, 80, 1, 2, 3],
            "temperature_2m_max": [24.5, 19.0, 17.2, 22.8, 23.0, 21.0],
            "temperature_2m_min": [14.1, 12.3, 11.0, 15.5, 13.0, 12.0],
            "precipitation_probability_max": [10, 80, 65, 5, 20, 30],
            "precipitation_sum": [0.0, 5.2, 3.1, 0.0, 1.0, 2.0]
        }
    })json";

    forecast::ForecastData out;
    REQUIRE(forecast::parseForecastJson(json, out));
    CHECK_EQ(out.count, 4);
}

TEST_CASE("forecast parseForecastJson returns false for missing daily key") {
    forecast::ForecastData out;
    CHECK_FALSE(forecast::parseForecastJson(R"json({"other": {}})json", out));
}

TEST_CASE("forecast parseForecastJson returns false for missing time array") {
    const std::string json = R"json({
        "daily": {
            "weather_code": [3],
            "temperature_2m_max": [24.5],
            "temperature_2m_min": [14.1],
            "precipitation_probability_max": [10],
            "precipitation_sum": [0.0]
        }
    })json";
    forecast::ForecastData out;
    CHECK_FALSE(forecast::parseForecastJson(json, out));
}

TEST_CASE("forecast parseForecastJson returns false for missing weather_code array") {
    const std::string json = R"json({
        "daily": {
            "time": ["2026-05-11"],
            "temperature_2m_max": [24.5],
            "temperature_2m_min": [14.1],
            "precipitation_probability_max": [10],
            "precipitation_sum": [0.0]
        }
    })json";
    forecast::ForecastData out;
    CHECK_FALSE(forecast::parseForecastJson(json, out));
}

TEST_CASE("forecast parseForecastJson returns false for missing temperature array") {
    const std::string json = R"json({
        "daily": {
            "time": ["2026-05-11"],
            "weather_code": [3],
            "temperature_2m_min": [14.1],
            "precipitation_probability_max": [10],
            "precipitation_sum": [0.0]
        }
    })json";
    forecast::ForecastData out;
    CHECK_FALSE(forecast::parseForecastJson(json, out));
}

TEST_CASE("forecast parseForecastJson returns false for invalid JSON") {
    forecast::ForecastData out;
    CHECK_FALSE(forecast::parseForecastJson("not json {{{", out));
}

TEST_CASE("forecast openmeteoUrl encodes timezone with slash") {
    const auto url = forecast::openmeteoUrl(location(-26.2041f, 28.0473f, "Africa/Johannesburg"));
    CHECK(url.find("timezone=Africa%2FJohannesburg") != std::string::npos);
    CHECK(url.find("latitude=") != std::string::npos);
    CHECK(url.find("longitude=") != std::string::npos);
    CHECK(url.find("forecast_days=4") != std::string::npos);
}

TEST_CASE("forecast openmeteoUrl formats coordinates with decimal point not comma") {
    const auto url = forecast::openmeteoUrl(location(-26.2041f, 28.0473f, "UTC"));
    CHECK(url.find("latitude=-26.2") != std::string::npos);
    CHECK(url.find("longitude=28.0") != std::string::npos);
}
