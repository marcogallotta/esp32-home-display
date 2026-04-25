#include "api/payloads.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"

#include <cstdint>
#include <optional>
#include <string>

namespace {

SensorIdentity identity() {
    return SensorIdentity{
        "AA:BB:CC:DD:EE:FF",
        "Kitchen",
        "K"
    };
}

SwitchbotReading completeSwitchbotReading() {
    SwitchbotReading reading;
    reading.temperatureC = 21.5f;
    reading.humidityPct = static_cast<std::uint8_t>(56);
    reading.lastSeenEpochS = 1710000000;
    return reading;
}

XiaomiReading completeXiaomiReading() {
    XiaomiReading reading;
    reading.temperatureC = 18.25f;
    reading.moisturePct = static_cast<std::uint8_t>(42);
    reading.lux = 1234;
    reading.conductivityUsCm = 567;
    reading.lastSeenEpochS = 1710000000;
    return reading;
}

StaticJsonDocument<512> parseJson(const std::string& text) {
    StaticJsonDocument<512> doc;
    const DeserializationError err = deserializeJson(doc, text);
    REQUIRE_FALSE(err);
    return doc;
}

bool hasKey(const JsonDocument& doc, const char* key) {
    return !doc[key].isNull();
}

void checkCommonPayloadFields(const JsonDocument& doc, const char* expectedType) {
    CHECK_EQ(doc["mac"].as<std::string>(), "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(doc["name"].as<std::string>(), "Kitchen");
    CHECK_EQ(doc["type"].as<std::string>(), expectedType);
    CHECK_EQ(doc["timestamp"].as<std::string>(), "2024-03-09T16:00:00Z");
}

} // namespace

TEST_CASE("switchbot payload includes required fields and complete reading values") {
    const auto payload = api::makeSwitchbotPayload(identity(), completeSwitchbotReading());
    REQUIRE(payload.has_value());

    const auto doc = parseJson(api::toJson(*payload));

    checkCommonPayloadFields(doc, "switchbot");
    CHECK_EQ(doc["temperature_c"].as<float>(), doctest::Approx(21.5f));
    CHECK_EQ(doc["humidity_pct"].as<int>(), 56);
}

TEST_CASE("switchbot payload is rejected unless temperature humidity and timestamp are present") {
    SUBCASE("missing temperature") {
        auto reading = completeSwitchbotReading();
        reading.temperatureC = std::nullopt;
        CHECK_FALSE(api::makeSwitchbotPayload(identity(), reading).has_value());
    }

    SUBCASE("missing humidity") {
        auto reading = completeSwitchbotReading();
        reading.humidityPct = std::nullopt;
        CHECK_FALSE(api::makeSwitchbotPayload(identity(), reading).has_value());
    }

    SUBCASE("missing timestamp") {
        auto reading = completeSwitchbotReading();
        reading.lastSeenEpochS = std::nullopt;
        CHECK_FALSE(api::makeSwitchbotPayload(identity(), reading).has_value());
    }

    SUBCASE("non-positive timestamp") {
        auto reading = completeSwitchbotReading();
        reading.lastSeenEpochS = 0;
        CHECK_FALSE(api::makeSwitchbotPayload(identity(), reading).has_value());
    }
}

TEST_CASE("xiaomi combined payload includes only present reading fields") {
    XiaomiReading reading;
    reading.temperatureC = 18.25f;
    reading.lux = 1234;
    reading.lastSeenEpochS = 1710000000;

    const auto payload = api::makeXiaomiPayload(identity(), reading);
    REQUIRE(payload.has_value());

    const auto doc = parseJson(api::toJson(*payload));

    checkCommonPayloadFields(doc, "xiaomi");
    CHECK_EQ(doc["temperature_c"].as<float>(), doctest::Approx(18.25f));
    CHECK_EQ(doc["light_lux"].as<int>(), 1234);
    CHECK_FALSE(hasKey(doc, "moisture_pct"));
    CHECK_FALSE(hasKey(doc, "conductivity_us_cm"));
}

TEST_CASE("xiaomi combined payload is rejected without timestamp or reading fields") {
    SUBCASE("missing timestamp") {
        auto reading = completeXiaomiReading();
        reading.lastSeenEpochS = std::nullopt;
        CHECK_FALSE(api::makeXiaomiPayload(identity(), reading).has_value());
    }

    SUBCASE("non-positive timestamp") {
        auto reading = completeXiaomiReading();
        reading.lastSeenEpochS = 0;
        CHECK_FALSE(api::makeXiaomiPayload(identity(), reading).has_value());
    }

    SUBCASE("no reading fields") {
        XiaomiReading reading;
        reading.lastSeenEpochS = 1710000000;
        CHECK_FALSE(api::makeXiaomiPayload(identity(), reading).has_value());
    }
}

TEST_CASE("xiaomi single-field payload helpers include exactly the requested reading field") {
    SUBCASE("temperature") {
        const auto payload = api::makeXiaomiTemperaturePayload(identity(), completeXiaomiReading());
        REQUIRE(payload.has_value());
        const auto doc = parseJson(api::toJson(*payload));

        checkCommonPayloadFields(doc, "xiaomi");
        CHECK_EQ(doc["temperature_c"].as<float>(), doctest::Approx(18.25f));
        CHECK_FALSE(hasKey(doc, "moisture_pct"));
        CHECK_FALSE(hasKey(doc, "light_lux"));
        CHECK_FALSE(hasKey(doc, "conductivity_us_cm"));
    }

    SUBCASE("moisture") {
        const auto payload = api::makeXiaomiMoisturePayload(identity(), completeXiaomiReading());
        REQUIRE(payload.has_value());
        const auto doc = parseJson(api::toJson(*payload));

        checkCommonPayloadFields(doc, "xiaomi");
        CHECK_EQ(doc["moisture_pct"].as<int>(), 42);
        CHECK_FALSE(hasKey(doc, "temperature_c"));
        CHECK_FALSE(hasKey(doc, "light_lux"));
        CHECK_FALSE(hasKey(doc, "conductivity_us_cm"));
    }

    SUBCASE("lux") {
        const auto payload = api::makeXiaomiLuxPayload(identity(), completeXiaomiReading());
        REQUIRE(payload.has_value());
        const auto doc = parseJson(api::toJson(*payload));

        checkCommonPayloadFields(doc, "xiaomi");
        CHECK_EQ(doc["light_lux"].as<int>(), 1234);
        CHECK_FALSE(hasKey(doc, "temperature_c"));
        CHECK_FALSE(hasKey(doc, "moisture_pct"));
        CHECK_FALSE(hasKey(doc, "conductivity_us_cm"));
    }

    SUBCASE("conductivity") {
        const auto payload = api::makeXiaomiConductivityPayload(identity(), completeXiaomiReading());
        REQUIRE(payload.has_value());
        const auto doc = parseJson(api::toJson(*payload));

        checkCommonPayloadFields(doc, "xiaomi");
        CHECK_EQ(doc["conductivity_us_cm"].as<int>(), 567);
        CHECK_FALSE(hasKey(doc, "temperature_c"));
        CHECK_FALSE(hasKey(doc, "moisture_pct"));
        CHECK_FALSE(hasKey(doc, "light_lux"));
    }
}

TEST_CASE("xiaomi single-field payload helpers reject missing requested field") {
    XiaomiReading reading;
    reading.lastSeenEpochS = 1710000000;

    CHECK_FALSE(api::makeXiaomiTemperaturePayload(identity(), reading).has_value());
    CHECK_FALSE(api::makeXiaomiMoisturePayload(identity(), reading).has_value());
    CHECK_FALSE(api::makeXiaomiLuxPayload(identity(), reading).has_value());
    CHECK_FALSE(api::makeXiaomiConductivityPayload(identity(), reading).has_value());
}
