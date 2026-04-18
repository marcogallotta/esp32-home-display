#include "config.h"

#include <ArduinoJson.h>
#include <iostream>
#include <string>

bool parseConfigText(const std::string& text, Config& config, bool logErrors) {
    StaticJsonDocument<4096> json;
    const DeserializationError err = deserializeJson(json, text.c_str(), text.size());
    if (err) {
        if (logErrors) {
            std::cerr << "Failed to parse config JSON: " << err.c_str() << std::endl;
        }
        return false;
    }

    auto fail = [&](const char* msg) {
        if (logErrors) {
            std::cerr << "Failed to parse config JSON: " << msg << std::endl;
        }
        return false;
    };

    const JsonObject forecast = json["forecast"];
    if (forecast.isNull()) {
        return fail("forecast is not an object");
    }
    if (!forecast["openmeteo_pem"].is<const char*>()) {
        return fail("forecast.openmeteo_pem is not a string");
    }
    if (!forecast["update_interval_minutes"].is<int>()) {
        return fail("forecast.update_interval_minutes is not an int");
    }

    const char* openmeteoPem = forecast["openmeteo_pem"].as<const char*>();
    const int updateIntervalMinutes = forecast["update_interval_minutes"].as<int>();
    if (updateIntervalMinutes <= 0) {
        return fail("forecast.update_interval_minutes must be > 0");
    }

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
    if (!salah["dst_rule"].is<const char*>()) {
        return fail("salah.dst_rule is not a string");
    }
    if (!salah["asr_makruh_minutes"].is<int>()) {
        return fail("salah.asr_makruh_minutes is not an int");
    }
    if (!salah["hanafi_asr"].is<bool>()) {
        return fail("salah.hanafi_asr is not a bool");
    }

    const int timezoneOffsetMinutes = salah["timezone_offset_minutes"].as<int>();
    const char* dstRule = salah["dst_rule"].as<const char*>();
    const int asrMakruhMinutes = salah["asr_makruh_minutes"].as<int>();
    const bool hanafiAsr = salah["hanafi_asr"].as<bool>();

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
    if (sensors.isNull()) {
        return fail("switchbot.sensors is not an array");
    }

    config.switchbot.sensors.clear();

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

        SwitchbotSensorConfig sensor;
        sensor.mac = s["mac"].as<const char*>();
        sensor.name = s["name"].as<const char*>();
        sensor.shortName = s["short_name"].as<const char*>();

        config.switchbot.sensors.push_back(sensor);
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

    config.forecast.openmeteoPem = openmeteoPem;
    config.forecast.updateIntervalMinutes = updateIntervalMinutes;

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