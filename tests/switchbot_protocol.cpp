#include "switchbot/protocol.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

namespace {

SwitchbotConfig switchbotConfig() {
    SwitchbotConfig config;
    config.sensors.push_back(SwitchbotSensorConfig{
        "AA:BB:CC:DD:EE:FF",
        "Bedroom Meter",
        "Bed",
    });
    return config;
}

std::vector<std::uint8_t> switchbotPayload(
    std::uint8_t tempDecimal,
    std::uint8_t tempIntRaw,
    std::uint8_t humidityRaw
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

} // namespace

TEST_CASE("switchbot meter payload rejects short payloads") {
    CHECK_FALSE(switchbot::isMeterPayload({0x01, 0x02, 0x03}));

    std::vector<std::uint8_t> almostLongEnough(11, 0x01);
    CHECK_FALSE(switchbot::isMeterPayload(almostLongEnough));
}

TEST_CASE("switchbot meter payload rejects payloads with only zero data after mac") {
    std::vector<std::uint8_t> payload(12, 0x00);
    payload[0] = 0xAA;
    payload[1] = 0xBB;
    payload[2] = 0xCC;
    payload[3] = 0xDD;
    payload[4] = 0xEE;
    payload[5] = 0xFF;

    CHECK_FALSE(switchbot::isMeterPayload(payload));
}

TEST_CASE("switchbot meter payload accepts non-zero data after mac") {
    auto payload = switchbotPayload(0x04, 0x97, 0x48);

    CHECK(switchbot::isMeterPayload(payload));
}

TEST_CASE("switchbot decode rejects short payloads") {
    auto decoded = switchbot::decodeMeter(
        "AA:BB:CC:DD:EE:FF",
        std::vector<std::uint8_t>(10, 0x00),
        switchbotConfig()
    );

    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("switchbot decode rejects unknown sensors") {
    auto decoded = switchbot::decodeMeter(
        "00:11:22:33:44:55",
        switchbotPayload(0x04, 0x97, 0x48),
        switchbotConfig()
    );

    CHECK_FALSE(decoded.has_value());
}

TEST_CASE("switchbot decode returns configured identity and positive temperature") {
    auto decoded = switchbot::decodeMeter(
        "AA:BB:CC:DD:EE:FF",
        switchbotPayload(0x04, 0x97, 0x48),
        switchbotConfig()
    );

    REQUIRE(decoded.has_value());
    CHECK(decoded->address == "AA:BB:CC:DD:EE:FF");
    CHECK(decoded->name == "Bedroom Meter");
    CHECK(decoded->shortName == "Bed");
    CHECK(decoded->temperature_c == doctest::Approx(23.4f));
    CHECK(decoded->humidity == 72);
}

TEST_CASE("switchbot decode handles negative temperature") {
    auto decoded = switchbot::decodeMeter(
        "AA:BB:CC:DD:EE:FF",
        switchbotPayload(0x06, 0x05, 0x42),
        switchbotConfig()
    );

    REQUIRE(decoded.has_value());
    CHECK(decoded->temperature_c == doctest::Approx(-5.6f));
    CHECK(decoded->humidity == 66);
}

TEST_CASE("switchbot decode masks humidity high bit") {
    auto decoded = switchbot::decodeMeter(
        "AA:BB:CC:DD:EE:FF",
        switchbotPayload(0x00, 0x95, 0xC8),
        switchbotConfig()
    );

    REQUIRE(decoded.has_value());
    CHECK(decoded->humidity == 72);
}
