#include "xiaomi/ble.h"

#include "ble/scanner.h"
#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

namespace {

constexpr const char* kKnownMac = "AA:BB:CC:DD:EE:FF";
constexpr const char* kUnknownMac = "00:11:22:33:44:55";
constexpr const char* kFe95Uuid = "0000fe95-0000-1000-8000-00805f9b34fb";
constexpr const char* kWrongUuid = "0000180f-0000-1000-8000-00805f9b34fb";

XiaomiConfig xiaomiConfig() {
    XiaomiConfig config;
    config.sensors.push_back(XiaomiSensorConfig{
        kKnownMac,
        "Basil Sensor",
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

ble::AdvertisementEvent xiaomiAdvertisement(
    const char* address,
    const std::string& uuid,
    const std::vector<std::uint8_t>& payload
) {
    ble::AdvertisementEvent event;
    event.address = address;
    event.serviceData[uuid] = payload;
    return event;
}

} // namespace

TEST_CASE("xiaomi scanner updates snapshot for known temperature advertisement") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(
        kKnownMac,
        kFe95Uuid,
        xiaomiPayload(0x1004, {0xEA, 0x00})
    ));

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);

    const auto it = snapshot.find(kKnownMac);
    REQUIRE(it != snapshot.end());
    CHECK(it->second.name == "Basil Sensor");
    CHECK(it->second.shortName == "Basil");
    CHECK(it->second.hasTemperature);
    CHECK(it->second.temperatureC == doctest::Approx(23.4f));
    CHECK_FALSE(it->second.hasMoisture);
    CHECK_FALSE(it->second.hasLux);
    CHECK_FALSE(it->second.hasConductivity);
    CHECK(it->second.lastSeenEpochS > 0);
}

TEST_CASE("xiaomi scanner accumulates separate object advertisements into one reading") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1008, {42})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1007, {0x56, 0x34, 0x12})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1009, {0xF4, 0x01})));

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);

    const auto reading = snapshot.at(kKnownMac);
    CHECK(reading.hasTemperature);
    CHECK(reading.temperatureC == doctest::Approx(23.4f));
    CHECK(reading.hasMoisture);
    CHECK_EQ(reading.moisturePct, 42);
    CHECK(reading.hasLux);
    CHECK_EQ(reading.lux, 0x123456);
    CHECK(reading.hasConductivity);
    CHECK_EQ(reading.conductivityUsCm, 500);
}

TEST_CASE("xiaomi scanner ignores unknown sensors") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(
        kUnknownMac,
        kFe95Uuid,
        xiaomiPayload(0x1004, {0xEA, 0x00})
    ));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("xiaomi scanner ignores unrelated service data uuid") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(
        kKnownMac,
        kWrongUuid,
        xiaomiPayload(0x1004, {0xEA, 0x00})
    ));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("xiaomi scanner ignores malformed xiaomi payload") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, {0x01, 0x02, 0x03}));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("xiaomi scanner ignores unknown object ids") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x9999, {0x01})));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("xiaomi scanner callback fires only when decoded value changes") {
    xiaomi::Scanner scanner(xiaomiConfig());
    int callbackCount = 0;
    scanner.setUpdateCallback([&]() { ++callbackCount; });

    scanner.handleAdvertisement(xiaomiAdvertisement(kUnknownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kWrongUuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, {0x01, 0x02, 0x03}));
    CHECK_EQ(callbackCount, 0);

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    CHECK_EQ(callbackCount, 1);

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    CHECK_EQ(callbackCount, 1);

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xF4, 0x00})));
    CHECK_EQ(callbackCount, 2);
}

TEST_CASE("xiaomi scanner preserves existing reading when later advertisement is malformed") {
    xiaomi::Scanner scanner(xiaomiConfig());

    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, xiaomiPayload(0x1004, {0xEA, 0x00})));
    scanner.handleAdvertisement(xiaomiAdvertisement(kKnownMac, kFe95Uuid, {0x01, 0x02, 0x03}));

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);
    const auto reading = snapshot.at(kKnownMac);
    CHECK(reading.hasTemperature);
    CHECK(reading.temperatureC == doctest::Approx(23.4f));
}
