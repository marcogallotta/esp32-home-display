#pragma once

#include <string>

#include "../config.h"

namespace forecast {

struct DayForecast {
    std::string date;
    int weatherCode = 0;
    float tempMax = 0;
    float tempMin = 0;
    int rainProbMax = 0;
};

struct ForecastData {
    DayForecast days[4];
    int count = 0;
};

std::string openmeteoUrl(const LocationConfig& location);

bool parseForecastJson(const std::string& json, ForecastData& out);

} // namespace forecast
