#pragma once

#include <string>
#include <vector>

struct ForecastConfig {
    std::string openmeteoPem;
    int updateIntervalMinutes;
};

struct LocationConfig {
    // Longitude and latitude in degrees. Used for both Salah and OpenMeteo API.
    float latitude;
    float longitude;
    // Timezone name, e.g. "Europe/Paris". Used for OpenMeteo API.
    std::string timezone;
    // TZ environment variable format, e.g. "CET-1CEST,M3.5.0/2,M10.5.0/3".
    // Used to initialise local time. Currently this only affects Salah time calculations.
    std::string timezoneLong;
};

struct SalahConfig {
    int timezoneOffsetMinutes;
    // "eu" for European rules, "none" for no adjustments.
    std::string dstRule;
    // Makruh time before Maghrib.
    int asrMakruhMinutes;
    bool hanafiAsr;
};

struct SwitchbotSensorConfig {
    std::string mac;
    std::string name;
    // Display name on ESP32
    std::string shortName;
};

struct SwitchbotConfig {
    // Up to 4 sensors supported on ESP32 due to screen space limitations.
    std::vector<SwitchbotSensorConfig> sensors;
};

struct WifiConfig {
    std::string ssid;
    std::string password;
};

struct Config {
    ForecastConfig forecast;
    LocationConfig location;
    SalahConfig salah;
    SwitchbotConfig switchbot;
    WifiConfig wifi;
};

bool loadConfig(Config& config);
bool parseConfigText(const std::string& text, Config& config, bool logErrors = true);
