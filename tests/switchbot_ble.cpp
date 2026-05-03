#include "switchbot/ble.h"

#include "ble/scanner.h"
#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint16_t kSwitchbotManufacturerId = 2409;
constexpr const char* kKnownMac = "AA:BB:CC:DD:EE:FF";
constexpr const char* kUnknownMac = "00:11:22:33:44:55";

SwitchbotConfig switchbotConfig() {
    SwitchbotConfig config;
    config.sensors.push_back(SwitchbotSensorConfig{
        kKnownMac,
        "Bedroom Meter",
        "Bed",
    });
    return config;
}

std::vector<std::uint8_t> switchbotPayload(
    std::uint8_t tempDecimal = 0x04,
    std::uint8_t tempIntRaw = 0x97,
    std::uint8_t humidityRaw = 0x48
) {
    return {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x00, 0x00,
        tempDecimal,
        tempIntRaw,
        humidityRaw,
        0x01,
    };
}

ble::AdvertisementEvent switchbotAdvertisement(
    const char* address,
    const std::vector<std::uint8_t>& payload
) {
    ble::AdvertisementEvent event;
    event.address = address;
    event.manufacturerData[kSwitchbotManufacturerId] = payload;
    return event;
}

} // namespace

TEST_CASE("switchbot scanner updates snapshot for known valid meter advertisement") {
    switchbot::Scanner scanner(switchbotConfig());

    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, switchbotPayload()));

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);

    const auto it = snapshot.find(kKnownMac);
    REQUIRE(it != snapshot.end());
    CHECK(it->second.name == "Bedroom Meter");
    CHECK(it->second.shortName == "Bed");
    CHECK(it->second.temperature_c == doctest::Approx(23.4f));
    CHECK_EQ(it->second.humidity, 72);
    CHECK(it->second.last_seen_epoch_s > 0);
}

TEST_CASE("switchbot scanner ignores advertisements without switchbot manufacturer data") {
    switchbot::Scanner scanner(switchbotConfig());
    ble::AdvertisementEvent event;
    event.address = kKnownMac;
    event.manufacturerData[1234] = switchbotPayload();

    scanner.handleAdvertisement(event);

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("switchbot scanner ignores unknown configured sensors") {
    switchbot::Scanner scanner(switchbotConfig());

    scanner.handleAdvertisement(switchbotAdvertisement(kUnknownMac, switchbotPayload()));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("switchbot scanner ignores all-zero meter payloads") {
    switchbot::Scanner scanner(switchbotConfig());
    std::vector<std::uint8_t> payload(12, 0x00);
    payload[0] = 0xAA;
    payload[1] = 0xBB;
    payload[2] = 0xCC;
    payload[3] = 0xDD;
    payload[4] = 0xEE;
    payload[5] = 0xFF;

    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, payload));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("switchbot scanner ignores short meter payloads") {
    switchbot::Scanner scanner(switchbotConfig());

    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, {0x01, 0x02, 0x03}));

    CHECK(scanner.snapshot().empty());
}

TEST_CASE("switchbot scanner callback fires only for accepted advertisements") {
    switchbot::Scanner scanner(switchbotConfig());
    int callbackCount = 0;
    scanner.setUpdateCallback([&]() { ++callbackCount; });

    scanner.handleAdvertisement(switchbotAdvertisement(kUnknownMac, switchbotPayload()));
    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, {0x01, 0x02, 0x03}));
    CHECK_EQ(callbackCount, 0);

    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, switchbotPayload()));
    CHECK_EQ(callbackCount, 1);

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);
}

TEST_CASE("switchbot scanner overwrites snapshot with latest valid reading") {
    switchbot::Scanner scanner(switchbotConfig());

    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, switchbotPayload(0x04, 0x97, 0x48)));
    scanner.handleAdvertisement(switchbotAdvertisement(kKnownMac, switchbotPayload(0x06, 0x05, 0x42)));

    const auto snapshot = scanner.snapshot();
    REQUIRE_EQ(snapshot.size(), 1U);
    const auto reading = snapshot.at(kKnownMac);
    CHECK(reading.temperature_c == doctest::Approx(-5.6f));
    CHECK_EQ(reading.humidity, 66);
}
