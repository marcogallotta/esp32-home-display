#include "config.h"

#include <ArduinoJson.h>
#include <iostream>
#include <string>

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

    auto readOptionalInt = [&](JsonObject obj, const char* key, int& value, const char* path) {
        const JsonVariant field = obj[key];
        if (field.isNull()) {
            return true;
        }
        if (!field.is<int>()) {
            return fail(std::string(path) + " is not an int");
        }
        value = field.as<int>();
        return true;
    };

    auto readOptionalFloat = [&](JsonObject obj, const char* key, float& value, const char* path) {
        const JsonVariant field = obj[key];
        if (field.isNull()) {
            return true;
        }
        if (!field.is<float>()) {
            return fail(std::string(path) + " is not a number");
        }
        value = field.as<float>();
        return true;
    };

    auto readOptionalUint32 = [&](JsonObject obj, const char* key, std::uint32_t& value, const char* path) {
        const JsonVariant field = obj[key];
        if (field.isNull()) {
            return true;
        }
        if (!field.is<unsigned int>()) {
            return fail(std::string(path) + " is not an unsigned int");
        }
        value = field.as<std::uint32_t>();
        return true;
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

    ApiBufferConfig apiBufferConfig = config.api.buffer;
    const JsonObject apiBuffer = api["buffer"];
    if (!apiBuffer.isNull() &&
        (!readOptionalInt(apiBuffer, "in_memory", apiBufferConfig.inMemory, "api.buffer.in_memory") ||
         !readOptionalUint32(apiBuffer, "disk_reserve_bytes", apiBufferConfig.diskReserveBytes, "api.buffer.disk_reserve_bytes") ||
         !readOptionalInt(apiBuffer, "drain_rate_cap", apiBufferConfig.drainRateCap, "api.buffer.drain_rate_cap") ||
         !readOptionalInt(apiBuffer, "drain_rate_tick_s", apiBufferConfig.drainRateTickS, "api.buffer.drain_rate_tick_s"))) {
        return false;
    }

    SensorWritePolicyConfig sensorWritePolicyConfig = config.api.sensorWritePolicy;
    const JsonObject sensorWritePolicy = api["sensor_write_policy"];
    if (!sensorWritePolicy.isNull() &&
        (!readOptionalInt(sensorWritePolicy, "heartbeat_minutes", sensorWritePolicyConfig.heartbeatMinutes, "api.sensor_write_policy.heartbeat_minutes") ||
         !readOptionalFloat(sensorWritePolicy, "temperature_delta_c", sensorWritePolicyConfig.temperatureDeltaC, "api.sensor_write_policy.temperature_delta_c") ||
         !readOptionalFloat(sensorWritePolicy, "humidity_delta_pct", sensorWritePolicyConfig.humidityDeltaPct, "api.sensor_write_policy.humidity_delta_pct") ||
         !readOptionalFloat(sensorWritePolicy, "moisture_delta_pct", sensorWritePolicyConfig.moistureDeltaPct, "api.sensor_write_policy.moisture_delta_pct") ||
         !readOptionalUint32(sensorWritePolicy, "conductivity_delta_us_cm", sensorWritePolicyConfig.conductivityDeltaUsCm, "api.sensor_write_policy.conductivity_delta_us_cm") ||
         !readOptionalUint32(sensorWritePolicy, "lux_delta_cap", sensorWritePolicyConfig.luxDeltaCap, "api.sensor_write_policy.lux_delta_cap") ||
         !readOptionalFloat(sensorWritePolicy, "lux_delta_fraction", sensorWritePolicyConfig.luxDeltaFraction, "api.sensor_write_policy.lux_delta_fraction"))) {
        return false;
    }

    const char* apiBaseUrl = api["base_url"].as<const char*>();
    const char* apiKey = api["api_key"].as<const char*>();
    const char* apiPemFile = api["pem_file"].as<const char*>();

    if (apiBufferConfig.inMemory <= 0) {
        return fail("api.buffer.in_memory must be > 0");
    }
    if (apiBufferConfig.drainRateCap <= 0) {
        return fail("api.buffer.drain_rate_cap must be > 0");
    }
    if (apiBufferConfig.drainRateTickS <= 0) {
        return fail("api.buffer.drain_rate_tick_s must be > 0");
    }
    if (sensorWritePolicyConfig.heartbeatMinutes <= 0) {
        return fail("api.sensor_write_policy.heartbeat_minutes must be > 0");
    }
    if (sensorWritePolicyConfig.temperatureDeltaC <= 0.0f) {
        return fail("api.sensor_write_policy.temperature_delta_c must be > 0");
    }
    if (sensorWritePolicyConfig.humidityDeltaPct <= 0.0f) {
        return fail("api.sensor_write_policy.humidity_delta_pct must be > 0");
    }
    if (sensorWritePolicyConfig.moistureDeltaPct <= 0.0f) {
        return fail("api.sensor_write_policy.moisture_delta_pct must be > 0");
    }
    if (sensorWritePolicyConfig.conductivityDeltaUsCm == 0) {
        return fail("api.sensor_write_policy.conductivity_delta_us_cm must be > 0");
    }
    if (sensorWritePolicyConfig.luxDeltaCap == 0) {
        return fail("api.sensor_write_policy.lux_delta_cap must be > 0");
    }
    if (sensorWritePolicyConfig.luxDeltaFraction <= 0.0f) {
        return fail("api.sensor_write_policy.lux_delta_fraction must be > 0");
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

    const JsonObject xiaomi = json["xiaomi"];
    if (xiaomi.isNull()) {
        return fail("xiaomi is not an object");
    }
    if (!xiaomi["update_interval_minutes"].isNull() && !xiaomi["update_interval_minutes"].is<int>()) {
        return fail("xiaomi.update_interval_minutes is not an int");
    }

    const int xiaomiUpdateIntervalMinutes = xiaomi["update_interval_minutes"] | config.xiaomi.updateIntervalMinutes;
    if (xiaomiUpdateIntervalMinutes <= 0) {
        return fail("xiaomi.update_interval_minutes must be > 0");
    }

    const JsonArray xiaomiSensors = xiaomi["sensors"];
    if (!xiaomi["sensors"].isNull() && xiaomiSensors.isNull()) {
        return fail("xiaomi.sensors is not an array");
    }

    config.xiaomi.sensors.clear();

    for (JsonObject s : xiaomiSensors) {
        if (!s["mac"].is<const char*>()) {
            return fail("xiaomi.sensors[].mac is not a string");
        }
        if (!s["name"].is<const char*>()) {
            return fail("xiaomi.sensors[].name is not a string");
        }
        if (!s["short_name"].is<const char*>()) {
            return fail("xiaomi.sensors[].short_name is not a string");
        }

        XiaomiSensorConfig sensor;
        sensor.mac = s["mac"].as<const char*>();
        sensor.name = s["name"].as<const char*>();
        sensor.shortName = s["short_name"].as<const char*>();
        config.xiaomi.sensors.push_back(sensor);
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
    config.api.pem.clear();
    config.api.buffer = apiBufferConfig;
    config.api.sensorWritePolicy = sensorWritePolicyConfig;

    config.location.latitude = latitude;
    config.location.longitude = longitude;
    config.location.timezone = timezone;
    config.location.timezoneLong = timezoneLong;

    config.salah.timezoneOffsetMinutes = timezoneOffsetMinutes;
    config.salah.dstRule = dstRuleStr;
    config.salah.asrMakruhMinutes = asrMakruhMinutes;
    config.salah.hanafiAsr = hanafiAsr;

    config.xiaomi.updateIntervalMinutes = xiaomiUpdateIntervalMinutes;

    config.wifi.ssid = ssid;
    config.wifi.password = password;

    return true;
}
