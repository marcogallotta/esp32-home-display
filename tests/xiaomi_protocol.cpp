#include "xiaomi/protocol.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

namespace {

XiaomiConfig xiaomiConfig() {
    XiaomiConfig config;
    config.sensors.push_back(XiaomiSensorConfig{
        "AA:BB:CC:DD:EE:FF",
        "Basil",
        "Basil",
    });
    return config;
}

std::vector<std::uint8_t> xiaomiPayload(
    std::uint16_t objectId,
    const std::vector<std::uint8_t>& data
) {
    std::vector<std::uint8_t> payload(15, 0x00);
    payload[12] = static_cast<std::uint8_t>(objectId & 0xFF);
    payload[13] = static_cast<std::uint8_t>((objectId >> 8) & 0xFF);
    payload[14] = static_cast<std::uint8_t>(data.size());
    payload.insert(payload.end(), data.begin(), data.end());
    return payload;
}

} // namespace

TEST_CASE("xiaomi service data uuid accepts known FE95 forms") {
    CHECK(xiaomi::isXiaomiServiceDataUuid("fe95"));
    CHECK(xiaomi::isXiaomiServiceDataUuid("FE95"));
    CHECK(xiaomi::isXiaomiServiceDataUuid("0xfe95"));
    CHECK(xiaomi::isXiaomiServiceDataUuid("0xFE95"));
    CHECK(xiaomi::isXiaomiServiceDataUuid("0000fe95-0000-1000-8000-00805f9b34fb"));
    CHECK(xiaomi::isXiaomiServiceDataUuid("0000FE95-0000-1000-8000-00805F9B34FB"));
}

TEST_CASE("xiaomi service data uuid rejects unrelated values") {
    CHECK_FALSE(xiaomi::isXiaomiServiceDataUuid("180f"));
    CHECK_FALSE(xiaomi::isXiaomiServiceDataUuid("0000180f-0000-1000-8000-00805f9b34fb"));
    CHECK_FALSE(xiaomi::isXiaomiServiceDataUuid(""));
}

TEST_CASE("xiaomi decode rejects short payloads") {
    CHECK_FALSE(xiaomi::decodeObject(std::vector<std::uint8_t>(14, 0x00)).has_value());
}

TEST_CASE("xiaomi decode rejects payloads shorter than declared object length") {
    std::vector<std::uint8_t> payload(15, 0x00);
    payload[12] = 0x04;
    payload[13] = 0x10;
    payload[14] = 2;
    payload.push_back(0xEA);

    CHECK_FALSE(xiaomi::decodeObject(payload).has_value());
}

TEST_CASE("xiaomi decode temperature object") {
    auto decoded = xiaomi::decodeObject(xiaomiPayload(0x1004, {0xEA, 0x00}));

    REQUIRE(decoded.has_value());
    CHECK(decoded->kind == xiaomi::DecodedObject::Kind::Temperature);
    CHECK(decoded->temperatureC == doctest::Approx(23.4f));
}

TEST_CASE("xiaomi decode negative temperature object") {
    auto decoded = xiaomi::decodeObject(xiaomiPayload(0x1004, {0xDC, 0xFF}));

    REQUIRE(decoded.has_value());
    CHECK(decoded->kind == xiaomi::DecodedObject::Kind::Temperature);
    CHECK(decoded->temperatureC == doctest::Approx(-3.6f));
}

TEST_CASE("xiaomi decode lux object") {
    auto decoded = xiaomi::decodeObject(xiaomiPayload(0x1007, {0x56, 0x34, 0x12}));

    REQUIRE(decoded.has_value());
    CHECK(decoded->kind == xiaomi::DecodedObject::Kind::Lux);
    CHECK(decoded->lux == 0x123456);
}

TEST_CASE("xiaomi decode moisture object") {
    auto decoded = xiaomi::decodeObject(xiaomiPayload(0x1008, {42}));

    REQUIRE(decoded.has_value());
    CHECK(decoded->kind == xiaomi::DecodedObject::Kind::Moisture);
    CHECK(decoded->moisturePct == 42);
}

TEST_CASE("xiaomi decode conductivity object") {
    auto decoded = xiaomi::decodeObject(xiaomiPayload(0x1009, {0xF4, 0x01}));

    REQUIRE(decoded.has_value());
    CHECK(decoded->kind == xiaomi::DecodedObject::Kind::Conductivity);
    CHECK(decoded->conductivityUsCm == 500);
}

TEST_CASE("xiaomi decode rejects wrong length for known objects") {
    CHECK_FALSE(xiaomi::decodeObject(xiaomiPayload(0x1004, {0xEA})).has_value());
    CHECK_FALSE(xiaomi::decodeObject(xiaomiPayload(0x1007, {0x56, 0x34})).has_value());
    CHECK_FALSE(xiaomi::decodeObject(xiaomiPayload(0x1008, {42, 43})).has_value());
    CHECK_FALSE(xiaomi::decodeObject(xiaomiPayload(0x1009, {0xF4})).has_value());
}

TEST_CASE("xiaomi decode rejects unknown object ids") {
    CHECK_FALSE(xiaomi::decodeObject(xiaomiPayload(0x9999, {0x01})).has_value());
}

TEST_CASE("xiaomi find sensor config returns known sensors") {
    const XiaomiSensorConfig* sensor = xiaomi::findSensorConfig(
        xiaomiConfig(),
        "AA:BB:CC:DD:EE:FF"
    );

    REQUIRE(sensor != nullptr);
    CHECK(sensor->name == "Basil");
    CHECK(sensor->shortName == "Basil");
}

TEST_CASE("xiaomi find sensor config rejects unknown sensors") {
    CHECK(xiaomi::findSensorConfig(xiaomiConfig(), "00:11:22:33:44:55") == nullptr);
}
