#include "api/payloads.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"

#include <cstdint>
#include <limits>
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

namespace {

std::string expandBytes(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    const auto* data = bytes.empty() ? nullptr : bytes.data();
    const bool ok = api::expandCompact(nullptr, data, bytes.size(), nullptr, out);
    REQUIRE(ok);
    return out;
}

} // namespace

TEST_CASE("compact encode/expand round-trip: switchbot") {
    const auto payload = api::makeSwitchbotPayload(identity(), completeSwitchbotReading());
    REQUIRE(payload.has_value());

    const auto compact = api::encodeCompact(*payload);
    REQUIRE_FALSE(compact.empty());

    const auto doc = parseJson(expandBytes(compact));
    checkCommonPayloadFields(doc, "switchbot");
    CHECK_EQ(doc["temperature_c"].as<float>(), doctest::Approx(21.5f));
    CHECK_EQ(doc["humidity_pct"].as<int>(), 56);
}

TEST_CASE("compact encode returns empty on invalid inputs") {
    SUBCASE("bad MAC") {
        api::SwitchbotPayload payload;
        payload.mac = "not-a-mac";
        payload.name = "X";
        payload.epochS = 1710000000;
        payload.temperatureC = 20.0f;
        payload.humidityPct = 50;
        CHECK(api::encodeCompact(payload).empty());
    }

    SUBCASE("name too long") {
        api::SwitchbotPayload payload;
        payload.mac = "AA:BB:CC:DD:EE:FF";
        payload.name = std::string(256, 'X');
        payload.epochS = 1710000000;
        payload.temperatureC = 20.0f;
        payload.humidityPct = 50;
        CHECK(api::encodeCompact(payload).empty());
    }

    SUBCASE("negative epoch") {
        api::SwitchbotPayload payload;
        payload.mac = "AA:BB:CC:DD:EE:FF";
        payload.name = "X";
        payload.epochS = -1;
        payload.temperatureC = 20.0f;
        payload.humidityPct = 50;
        CHECK(api::encodeCompact(payload).empty());
    }

    SUBCASE("temperature NaN") {
        api::SwitchbotPayload payload;
        payload.mac = "AA:BB:CC:DD:EE:FF";
        payload.name = "X";
        payload.epochS = 1710000000;
        payload.temperatureC = std::numeric_limits<float>::quiet_NaN();
        payload.humidityPct = 50;
        CHECK(api::encodeCompact(payload).empty());
    }

    SUBCASE("temperature out of int16 range") {
        api::SwitchbotPayload payload;
        payload.mac = "AA:BB:CC:DD:EE:FF";
        payload.name = "X";
        payload.epochS = 1710000000;
        payload.temperatureC = 4000.0f;
        payload.humidityPct = 50;
        CHECK(api::encodeCompact(payload).empty());
    }
}

TEST_CASE("expandCompact rejects null data with nonzero size") {
    std::string out;
    CHECK_FALSE(api::expandCompact(nullptr, nullptr, 1, nullptr, out));
}

TEST_CASE("expandCompact rejects truncated input") {
    const auto payload = api::makeSwitchbotPayload(identity(), completeSwitchbotReading());
    REQUIRE(payload.has_value());
    const auto compact = api::encodeCompact(*payload);
    REQUIRE_FALSE(compact.empty());

    // Every prefix shorter than the full record should fail.
    for (std::size_t len = 0; len < compact.size() - 1; ++len) {
        std::string out;
        CHECK_FALSE(api::expandCompact(nullptr, compact.data(), len, nullptr, out));
    }
}
