#include "config.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr const char* kExampleConfigPath = "data/config.json.example";

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open test fixture: " + path);
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

DynamicJsonDocument parseJson(const std::string& text) {
    DynamicJsonDocument doc(8192);
    const DeserializationError err = deserializeJson(doc, text);
    if (err) {
        throw std::runtime_error(std::string("failed to parse JSON fixture: ") + err.c_str());
    }
    return doc;
}

DynamicJsonDocument exampleConfig() {
    return parseJson(readFile(kExampleConfigPath));
}

std::string toJson(const DynamicJsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool parses(const DynamicJsonDocument& doc, Config& config) {
    return parseConfigText(toJson(doc), config, false);
}

void expectValid(const DynamicJsonDocument& doc) {
    Config config;
    CHECK(parses(doc, config));
}

void expectInvalid(const DynamicJsonDocument& doc) {
    Config config;
    CHECK_FALSE(parses(doc, config));
}

void removePath(DynamicJsonDocument& doc, const char* section, const char* key) {
    doc[section].remove(key);
}

void removePath(DynamicJsonDocument& doc, const char* section, const char* object, const char* key) {
    doc[section][object].remove(key);
}

DynamicJsonDocument exampleWithoutOptionalFields() {
    auto doc = exampleConfig();

    removePath(doc, "forecast", "update_interval_minutes");
    removePath(doc, "api", "buffer");
    removePath(doc, "salah", "dst_rule");
    removePath(doc, "salah", "asr_makruh_minutes");
    removePath(doc, "salah", "hanafi_asr");
    removePath(doc, "switchbot", "sensors");
    removePath(doc, "xiaomi", "update_interval_minutes");
    removePath(doc, "xiaomi", "sensors");

    return doc;
}

JsonObject addSwitchbotSensor(DynamicJsonDocument& doc) {
    JsonArray sensors;
    if (doc["switchbot"]["sensors"].is<JsonArray>()) {
        sensors = doc["switchbot"]["sensors"].as<JsonArray>();
    } else {
        sensors = doc["switchbot"].createNestedArray("sensors");
    }

    JsonObject sensor = sensors.createNestedObject();
    sensor["mac"] = "AA:BB:CC:DD:EE:FF";
    sensor["name"] = "Room 1";
    sensor["short_name"] = "R1";
    return sensor;
}

JsonObject addXiaomiSensor(DynamicJsonDocument& doc) {
    JsonArray sensors;
    if (doc["xiaomi"]["sensors"].is<JsonArray>()) {
        sensors = doc["xiaomi"]["sensors"].as<JsonArray>();
    } else {
        sensors = doc["xiaomi"].createNestedArray("sensors");
    }

    JsonObject sensor = sensors.createNestedObject();
    sensor["mac"] = "11:22:33:44:55:66";
    sensor["name"] = "Plant 1";
    sensor["short_name"] = "P1";
    return sensor;
}

void checkDefaults(const Config& config) {
    const Config defaults{};

    CHECK_EQ(config.forecast.updateIntervalMinutes, defaults.forecast.updateIntervalMinutes);
    CHECK_EQ(config.api.buffer.inMemory, defaults.api.buffer.inMemory);
    CHECK_EQ(config.api.buffer.drainRateCap, defaults.api.buffer.drainRateCap);
    CHECK_EQ(config.api.buffer.drainRateTickS, defaults.api.buffer.drainRateTickS);
    CHECK_EQ(config.salah.dstRule, defaults.salah.dstRule);
    CHECK_EQ(config.salah.asrMakruhMinutes, defaults.salah.asrMakruhMinutes);
    CHECK_EQ(config.salah.hanafiAsr, defaults.salah.hanafiAsr);
    CHECK_EQ(config.xiaomi.updateIntervalMinutes, defaults.xiaomi.updateIntervalMinutes);
    CHECK(config.switchbot.sensors.empty());
    CHECK(config.xiaomi.sensors.empty());
}

TEST_CASE("config example is valid") {
    expectValid(exampleConfig());
}

TEST_CASE("config uses defaults when optional fields are omitted") {
    Config config;
    REQUIRE(parses(exampleWithoutOptionalFields(), config));
    checkDefaults(config);
}

TEST_CASE("config rejects malformed JSON") {
    Config config;
    CHECK_FALSE(parseConfigText(R"json({ "forecast": { "openmeteo_pem_file": "/openmeteo.pem", } })json",
                                config,
                                false));
}

TEST_CASE("config requires critical top-level sections") {
    for (const char* section : {"forecast", "api", "location", "salah", "wifi"}) {
        CAPTURE(section);
        auto doc = exampleConfig();
        doc.remove(section);
        expectInvalid(doc);
    }
}

TEST_CASE("config requires critical leaf fields") {
    struct Field {
        const char* section;
        const char* key;
    };

    const Field fields[] = {
        {"forecast", "openmeteo_pem_file"},
        {"api", "base_url"},
        {"api", "api_key"},
        {"api", "pem_file"},
        {"location", "latitude"},
        {"location", "longitude"},
        {"location", "timezone"},
        {"location", "timezone_long"},
        {"salah", "timezone_offset_minutes"},
        {"wifi", "ssid"},
        {"wifi", "password"},
    };

    for (const Field& field : fields) {
        CAPTURE(field.section);
        CAPTURE(field.key);
        auto doc = exampleConfig();
        removePath(doc, field.section, field.key);
        expectInvalid(doc);
    }
}

TEST_CASE("config validates forecast values") {
    SUBCASE("forecast pem file must be a string") {
        auto doc = exampleConfig();
        doc["forecast"]["openmeteo_pem_file"] = 123;
        expectInvalid(doc);
    }

    SUBCASE("forecast interval must be an int") {
        auto doc = exampleConfig();
        doc["forecast"]["update_interval_minutes"] = "30";
        expectInvalid(doc);
    }

    SUBCASE("forecast interval must be positive") {
        auto doc = exampleConfig();
        doc["forecast"]["update_interval_minutes"] = 0;
        expectInvalid(doc);
    }
}

TEST_CASE("config validates API values") {
    SUBCASE("API strings must be strings") {
        for (const char* key : {"base_url", "api_key", "pem_file"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            doc["api"][key] = 123;
            expectInvalid(doc);
        }
    }

    SUBCASE("API buffer values must be ints") {
        for (const char* key : {"in_memory", "drain_rate_cap", "drain_rate_tick_s"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            doc["api"]["buffer"][key] = "1";
            expectInvalid(doc);
        }
    }

    SUBCASE("API buffer values must be positive") {
        for (const char* key : {"in_memory", "drain_rate_cap", "drain_rate_tick_s"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            doc["api"]["buffer"][key] = 0;
            expectInvalid(doc);
        }
    }
}

TEST_CASE("config validates location values") {
    SUBCASE("latitude must be a number") {
        auto doc = exampleConfig();
        doc["location"]["latitude"] = "48.9";
        expectInvalid(doc);
    }

    SUBCASE("latitude must be in range") {
        auto doc = exampleConfig();
        doc["location"]["latitude"] = 200.0;
        expectInvalid(doc);
    }

    SUBCASE("longitude must be a number") {
        auto doc = exampleConfig();
        doc["location"]["longitude"] = "2.3";
        expectInvalid(doc);
    }

    SUBCASE("longitude must be in range") {
        auto doc = exampleConfig();
        doc["location"]["longitude"] = 200.0;
        expectInvalid(doc);
    }

    SUBCASE("timezone fields must be strings") {
        for (const char* key : {"timezone", "timezone_long"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            doc["location"][key] = 123;
            expectInvalid(doc);
        }
    }
}

TEST_CASE("config validates salah values") {
    SUBCASE("timezone offset must be an int") {
        auto doc = exampleConfig();
        doc["salah"]["timezone_offset_minutes"] = "60";
        expectInvalid(doc);
    }

    SUBCASE("timezone offset must be in range") {
        auto doc = exampleConfig();
        doc["salah"]["timezone_offset_minutes"] = 900;
        expectInvalid(doc);
    }

    SUBCASE("dst rule must be a string") {
        auto doc = exampleConfig();
        doc["salah"]["dst_rule"] = true;
        expectInvalid(doc);
    }

    SUBCASE("dst rule must be supported") {
        auto doc = exampleConfig();
        doc["salah"]["dst_rule"] = "mars";
        expectInvalid(doc);
    }

    SUBCASE("asr makruh must be an int") {
        auto doc = exampleConfig();
        doc["salah"]["asr_makruh_minutes"] = "20";
        expectInvalid(doc);
    }

    SUBCASE("asr makruh cannot be negative") {
        auto doc = exampleConfig();
        doc["salah"]["asr_makruh_minutes"] = -1;
        expectInvalid(doc);
    }

    SUBCASE("hanafi_asr must be a bool") {
        auto doc = exampleConfig();
        doc["salah"]["hanafi_asr"] = "false";
        expectInvalid(doc);
    }
}

TEST_CASE("config validates switchbot sensors") {
    SUBCASE("sensors must be an array when present") {
        auto doc = exampleConfig();
        doc["switchbot"]["sensors"] = 123;
        expectInvalid(doc);
    }

    SUBCASE("sensor fields must be strings") {
        for (const char* key : {"mac", "name", "short_name"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            JsonObject sensor = addSwitchbotSensor(doc);
            sensor[key] = 123;
            expectInvalid(doc);
        }
    }

    SUBCASE("no sensors is valid") {
        auto doc = exampleConfig();
        removePath(doc, "switchbot", "sensors");
        expectValid(doc);
    }
}

TEST_CASE("config validates xiaomi values") {
    SUBCASE("interval must be an int") {
        auto doc = exampleConfig();
        doc["xiaomi"]["update_interval_minutes"] = "60";
        expectInvalid(doc);
    }

    SUBCASE("interval must be positive") {
        auto doc = exampleConfig();
        doc["xiaomi"]["update_interval_minutes"] = 0;
        expectInvalid(doc);
    }

    SUBCASE("sensors must be an array when present") {
        auto doc = exampleConfig();
        doc["xiaomi"]["sensors"] = 123;
        expectInvalid(doc);
    }

    SUBCASE("sensor fields must be strings") {
        for (const char* key : {"mac", "name", "short_name"}) {
            CAPTURE(key);
            auto doc = exampleConfig();
            JsonObject sensor = addXiaomiSensor(doc);
            sensor[key] = 123;
            expectInvalid(doc);
        }
    }

    SUBCASE("no sensors is valid") {
        auto doc = exampleConfig();
        removePath(doc, "xiaomi", "sensors");
        expectValid(doc);
    }
}

TEST_CASE("config validates wifi values") {
    for (const char* key : {"ssid", "password"}) {
        CAPTURE(key);
        auto doc = exampleConfig();
        doc["wifi"][key] = 123;
        expectInvalid(doc);
    }
}

} // namespace
