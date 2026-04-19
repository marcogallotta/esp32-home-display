#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include <ArduinoJson.h>

#include "../config.h"
#include "openmeteo.h"

namespace forecast {

std::string urlEncode(const std::string& s) {
    std::string out;
    char hex[4];
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string formatFloat(float value) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

std::string openmeteoUrl(const LocationConfig& location) {
    return "https://api.open-meteo.com/v1/forecast?" \
        "latitude=" + formatFloat(location.latitude) + "&" \
        "longitude=" + formatFloat(location.longitude) + "&" \
        "daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum&" \
        "timezone=" + urlEncode(location.timezone) + "&" \
        "forecast_days=4";
}

bool parseForecastJson(const std::string& json, ForecastData& out) {
    // Using a fixed-size for simplicity.
    StaticJsonDocument<2048> doc;

    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return false;
    }

    JsonObject daily = doc["daily"];
    if (daily.isNull()) {
        return false;
    }

    JsonArray time = daily["time"];
    JsonArray weatherCode = daily["weather_code"];
    JsonArray tempMax = daily["temperature_2m_max"];
    JsonArray tempMin = daily["temperature_2m_min"];
    JsonArray rainProbMax = daily["precipitation_probability_max"];

    if (time.isNull() || weatherCode.isNull() || tempMax.isNull() ||
        tempMin.isNull() || rainProbMax.isNull()) {
        return false;
    }

    const int n = std::min<int>({
        4,
        static_cast<int>(time.size()),
        static_cast<int>(weatherCode.size()),
        static_cast<int>(tempMax.size()),
        static_cast<int>(tempMin.size()),
        static_cast<int>(rainProbMax.size())
    });
    out.count = n;

    for (int i = 0; i < n; ++i) {
        out.days[i].date = time[i].as<const char*>();
        out.days[i].weatherCode = weatherCode[i] | 0;
        out.days[i].tempMax = tempMax[i] | 0.0f;
        out.days[i].tempMin = tempMin[i] | 0.0f;
        out.days[i].rainProbMax = rainProbMax[i] | 0;
    }

    return true;
}

} // namespace forecast
