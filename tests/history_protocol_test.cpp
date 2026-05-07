#include "switchbot/history_protocol.h"

#include <doctest/doctest.h>

using namespace switchbot::history;

TEST_CASE("SwitchBot Outdoor Meter history command builders") {
    CHECK(bytesToHex(buildTimeSyncCommand(0x69fc355d)) == "570005030d0000000069fc355d");
    CHECK(bytesToHex(buildStartCommand()) == "570f3a");
    CHECK(bytesToHex(buildMetadataCommand()) == "570f3b00");
    CHECK(bytesToHex(buildPageCommand(0x00012d04)) == "570f3c0000012d0406");
}

TEST_CASE("SwitchBot Outdoor Meter metadata response parses") {
    Metadata metadata;
    REQUIRE(parseMetadataResponse(hexToBytes("0169b59d5269fc354600012d34003c"), metadata));
    CHECK(metadata.startEpoch == 1773509970U);
    CHECK(metadata.endEpoch == 1778136390U);
    CHECK(metadata.endIndex == 0x00012d34U);
    CHECK(metadata.intervalSeconds == 60U);
}

TEST_CASE("SwitchBot Outdoor Meter index epoch conversion") {
    const uint32_t startEpoch = 1773509975U;
    CHECK(indexToEpoch(77061, startEpoch, 60) == 1778133635U);
    CHECK(epochToIndex(1778133635U, startEpoch, 60) == 77061U);
}

TEST_CASE("SwitchBot Outdoor Meter page decoder decodes six samples") {
    const uint32_t startEpoch = 1773509975U;
    const uint16_t interval = 60;
    auto samples = decodePageResponse(hexToBytes("018b32668b328b32668b328b32668b32"),
                                      77061,
                                      startEpoch,
                                      interval);
    REQUIRE(samples.size() == 6);
    CHECK(samples[0].index == 77061U);
    CHECK(samples[0].epoch == 1778133635U);
    CHECK(samples[0].temperatureC == doctest::Approx(11.6));
    CHECK(samples[0].humidityPct == 50);
    CHECK(samples[5].index == 77066U);
    CHECK(samples[5].temperatureC == doctest::Approx(11.6));
    CHECK(samples[5].humidityPct == 50);
}

TEST_CASE("SwitchBot Outdoor Meter page decoder handles mixed decimal nibbles") {
    const uint32_t startEpoch = 1773509975U;
    auto samples = decodePageResponse(hexToBytes("018b32688b328b32828c32"), 77067, startEpoch, 60);
    REQUIRE(samples.size() == 6);
    CHECK(samples[0].temperatureC == doctest::Approx(11.6));
    CHECK(samples[1].temperatureC == doctest::Approx(11.8));
    CHECK(samples[4].temperatureC == doctest::Approx(11.8));
    CHECK(samples[5].temperatureC == doctest::Approx(12.2));
    CHECK(samples[5].humidityPct == 50);
}

TEST_CASE("SwitchBot Outdoor Meter later page matches known decoded output") {
    const uint32_t startEpoch = 1773509975U;
    auto samples = decodePageResponse(hexToBytes("018c31558c318c31578c2f8c2f778c2f"), 77079, startEpoch, 60);
    REQUIRE(samples.size() == 6);
    CHECK(samples[0].temperatureC == doctest::Approx(12.5));
    CHECK(samples[0].humidityPct == 49);
    CHECK(samples[3].temperatureC == doctest::Approx(12.7));
    CHECK(samples[3].humidityPct == 47);
    CHECK(samples[5].temperatureC == doctest::Approx(12.7));
    CHECK(samples[5].humidityPct == 47);
}
