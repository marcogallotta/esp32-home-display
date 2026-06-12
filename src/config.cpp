#include "config.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <set>
#include <string>

namespace {

std::string normalizeMac(const std::string& mac) {
    std::string hex;
    hex.reserve(12);
    for (char c : mac) {
        if (c == ':' || c == '-' || std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return {};
        }
        hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (hex.size() != 12) {
        return {};
    }
    std::string out;
    out.reserve(17);
    for (std::size_t i = 0; i < hex.size(); ++i) {
        if (i != 0 && i % 2 == 0) {
            out.push_back(':');
        }
        out.push_back(hex[i]);
    }
    return out;
}

}  // namespace

bool parseConfigText(const std::string& text, Config& config, bool logErrors) {
    // Using a fixed-size for simplicity. You might need to adjust this based on your
    // expected config size and available memory.
    StaticJsonDocument<4096> json;
    const DeserializationError err = deserializeJson(json, text.c_str(), text.size());
    if (err) {
        if (logErrors) {
            std::cerr << "Failed to parse config JSON: " << err.c_str() << std::endl;
        }
        return false;
    }

    auto fail = [&](const std::string& msg) {
        if (logErrors) {
            std::cerr << "Failed to parse config JSON: " << msg << std::endl;
        }
        return false;
    };

    const JsonObject forecast = json["forecast"];
    if (forecast.isNull()) {
        return fail("forecast is not an object");
    }
    if (!forecast["openmeteo_pem_file"].is<const char*>()) {
        return fail("forecast.openmeteo_pem_file is not a string");
    }
    if (!forecast["update_interval_minutes"].isNull() && !forecast["update_interval_minutes"].is<int>()) {
        return fail("forecast.update_interval_minutes is not an int");
    }

    const char* openmeteoPemFile = forecast["openmeteo_pem_file"].as<const char*>();
    const int updateIntervalMinutes = forecast["update_interval_minutes"] | config.forecast.updateIntervalMinutes;
    if (updateIntervalMinutes <= 0) {
        return fail("forecast.update_interval_minutes must be > 0");
    }

    const JsonObject api = json["api"];
    if (api.isNull()) {
        return fail("api is not an object");
    }
    if (!api["base_url"].is<const char*>()) {
        return fail("api.base_url is not a string");
    }
    if (!api["api_key"].is<const char*>()) {
        return fail("api.api_key is not a string");
    }
    if (!api["pem_file"].is<const char*>()) {
        return fail("api.pem_file is not a string");
    }

    const char* apiBaseUrl = api["base_url"].as<const char*>();
    const char* apiKey = api["api_key"].as<const char*>();
    const char* apiPemFile = api["pem_file"].as<const char*>();

    const JsonObject location = json["location"];
    if (location.isNull()) {
        return fail("location is not an object");
    }
    if (!location["latitude"].is<float>()) {
        return fail("location.latitude is not a number");
    }
    if (!location["longitude"].is<float>()) {
        return fail("location.longitude is not a number");
    }
    if (!location["timezone"].is<const char*>()) {
        return fail("location.timezone is not a string");
    }
    if (!location["timezone_long"].is<const char*>()) {
        return fail("location.timezone_long is not a string");
    }

    const float latitude = location["latitude"].as<float>();
    const float longitude = location["longitude"].as<float>();
    const char* timezone = location["timezone"].as<const char*>();
    const char* timezoneLong = location["timezone_long"].as<const char*>();

    if (latitude < -90.0f || latitude > 90.0f) {
        return fail("location.latitude is out of range");
    }
    if (longitude < -180.0f || longitude > 180.0f) {
        return fail("location.longitude is out of range");
    }

    const JsonObject salah = json["salah"];
    if (salah.isNull()) {
        return fail("salah is not an object");
    }
    if (!salah["timezone_offset_minutes"].is<int>()) {
        return fail("salah.timezone_offset_minutes is not an int");
    }
    if (!salah["dst_rule"].isNull() && !salah["dst_rule"].is<const char*>()) {
        return fail("salah.dst_rule is not a string");
    }
    if (!salah["asr_makruh_minutes"].isNull() && !salah["asr_makruh_minutes"].is<int>()) {
        return fail("salah.asr_makruh_minutes is not an int");
    }
    if (!salah["hanafi_asr"].isNull() && !salah["hanafi_asr"].is<bool>()) {
        return fail("salah.hanafi_asr is not a bool");
    }

    const int timezoneOffsetMinutes = salah["timezone_offset_minutes"].as<int>();
    const char* dstRule = salah["dst_rule"] | config.salah.dstRule.c_str();
    const int asrMakruhMinutes = salah["asr_makruh_minutes"] | config.salah.asrMakruhMinutes;
    const bool hanafiAsr = salah["hanafi_asr"] | config.salah.hanafiAsr;

    if (timezoneOffsetMinutes < -720 || timezoneOffsetMinutes > 840) {
        return fail("salah.timezone_offset_minutes is out of range");
    }
    if (asrMakruhMinutes < 0) {
        return fail("salah.asr_makruh_minutes is out of range");
    }

    const std::string dstRuleStr(dstRule);
    if (dstRuleStr != "eu" && dstRuleStr != "none") {
        return fail("salah.dst_rule is not a supported value");
    }

    const JsonObject switchbot = json["switchbot"];
    if (switchbot.isNull()) {
        return fail("switchbot is not an object");
    }

    const JsonArray sensors = switchbot["sensors"];
    if (!switchbot["sensors"].isNull() && sensors.isNull()) {
        return fail("switchbot.sensors is not an array");
    }

    config.switchbot.sensors.clear();

    {
        std::set<std::string> seenMacs;
        for (JsonObject s : sensors) {
            if (!s["mac"].is<const char*>()) {
                return fail("switchbot.sensors[].mac is not a string");
            }
            if (!s["name"].is<const char*>()) {
                return fail("switchbot.sensors[].name is not a string");
            }
            if (!s["short_name"].is<const char*>()) {
                return fail("switchbot.sensors[].short_name is not a string");
            }

            const std::string normalized = normalizeMac(s["mac"].as<const char*>());
            if (normalized.empty()) {
                return fail("switchbot.sensors[].mac is invalid");
            }
            if (!seenMacs.insert(normalized).second) {
                return fail("switchbot.sensors[].mac is a duplicate");
            }

            SwitchbotSensorConfig sensor;
            sensor.mac = normalized;
            sensor.name = s["name"].as<const char*>();
            sensor.shortName = s["short_name"].as<const char*>();
            config.switchbot.sensors.push_back(sensor);
        }
    }

    const JsonObject wifi = json["wifi"];
    if (wifi.isNull()) {
        return fail("wifi is not an object");
    }
    if (!wifi["ssid"].is<const char*>()) {
        return fail("wifi.ssid is not a string");
    }
    if (!wifi["password"].is<const char*>()) {
        return fail("wifi.password is not a string");
    }

    const char* ssid = wifi["ssid"].as<const char*>();
    const char* password = wifi["password"].as<const char*>();

    config.forecast.openmeteoPemFile = openmeteoPemFile;
    config.forecast.openmeteoPem.clear();
    config.forecast.updateIntervalMinutes = updateIntervalMinutes;

    config.api.baseUrl = apiBaseUrl;
    config.api.apiKey = apiKey;
    config.api.pemFile = apiPemFile;

    config.location.latitude = latitude;
    config.location.longitude = longitude;
    config.location.timezone = timezone;
    config.location.timezoneLong = timezoneLong;

    config.salah.timezoneOffsetMinutes = timezoneOffsetMinutes;
    config.salah.dstRule = dstRuleStr;
    config.salah.asrMakruhMinutes = asrMakruhMinutes;
    config.salah.hanafiAsr = hanafiAsr;

    config.wifi.ssid = ssid;
    config.wifi.password = password;

    return true;
}
