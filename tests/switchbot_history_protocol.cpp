#include "switchbot/history_protocol.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> values) {
    return std::vector<std::uint8_t>(values);
}

constexpr std::uint8_t kSuccess = 0x01;
constexpr std::uint8_t kStartStatus = 0x55;
constexpr std::uint8_t kHeader = 0x57;
constexpr std::uint8_t kHistoryGroup = 0x0f;
constexpr std::uint8_t kMetadataCommand = 0x3b;
constexpr std::uint8_t kPageCommand = 0x3c;

std::uint32_t readU32BE(const std::vector<std::uint8_t>& values, std::size_t offset) {
    return (static_cast<std::uint32_t>(values[offset]) << 24) |
           (static_cast<std::uint32_t>(values[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(values[offset + 2]) << 8) |
           static_cast<std::uint32_t>(values[offset + 3]);
}

std::vector<std::uint8_t> startResponse(std::initializer_list<std::uint8_t> banks) {
    std::vector<std::uint8_t> response{kSuccess, kStartStatus};
    response.insert(response.end(), banks.begin(), banks.end());
    return response;
}

} // namespace

TEST_CASE("switchbot history builds known commands") {
    CHECK(switchbot::history::buildTimeSyncCommand(0x69fc355d) == bytes({
        0x57, 0x00, 0x05, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0x69, 0xfc, 0x35, 0x5d,
    }));

    CHECK(switchbot::history::buildStartCommand() == bytes({0x57, 0x0f, 0x3a}));
    CHECK(switchbot::history::buildMetadataCommand() == bytes({0x57, 0x0f, 0x3b, 0x00}));
    CHECK(switchbot::history::buildPageCommand(0x00012d04, 6) == bytes({
        0x57, 0x0f, 0x3c, 0x00, 0x00, 0x01, 0x2d, 0x04, 0x06,
    }));
}

TEST_CASE("switchbot history builds bank metadata command") {
    const std::uint8_t bank = 0x04;
    const auto command = switchbot::history::buildBankMetadataCommand(bank);

    REQUIRE_EQ(command.size(), 4U);
    CHECK_EQ(command[0], kHeader);
    CHECK_EQ(command[1], kHistoryGroup);
    CHECK_EQ(command[2], kMetadataCommand);
    CHECK_EQ(command[3], bank);
}

TEST_CASE("switchbot history builds bank page command") {
    const std::uint8_t bank = 0x04;
    const std::uint32_t bankLocalIndex = 0x00012d04;
    const std::uint8_t sampleCount = switchbot::history::kSamplesPerPage;
    const auto command = switchbot::history::buildBankPageCommand(bank, bankLocalIndex, sampleCount);

    REQUIRE_EQ(command.size(), 9U);
    CHECK_EQ(command[0], kHeader);
    CHECK_EQ(command[1], kHistoryGroup);
    CHECK_EQ(command[2], kPageCommand);
    CHECK_EQ(command[3], bank);
    CHECK_EQ(readU32BE(command, 4), bankLocalIndex);
    CHECK_EQ(command[8], sampleCount);
}

TEST_CASE("switchbot history parses start bank list") {
    const auto start = switchbot::history::parseStartResponse(startResponse({0x02, 0x01, 0x00, 0x04, 0x03}));

    REQUIRE(start.has_value());
    CHECK_EQ(start->status, kStartStatus);
    CHECK(start->banks == bytes({0x02, 0x01, 0x00, 0x04, 0x03}));
}

TEST_CASE("switchbot history rejects malformed start response") {
    CHECK_FALSE(switchbot::history::parseStartResponse({}).has_value());
    CHECK_FALSE(switchbot::history::parseStartResponse(bytes({0x02, kStartStatus, 0x00})).has_value());
    CHECK_FALSE(switchbot::history::parseStartResponse(bytes({kSuccess, kStartStatus})).has_value());
}

TEST_CASE("switchbot history parses metadata response") {
    const auto metadata = switchbot::history::parseMetadataResponse(bytes({
        0x01,
        0x69, 0xb5, 0x9d, 0x52,
        0x69, 0xfc, 0x35, 0x46,
        0x00, 0x01, 0x2d, 0x34,
        0x00, 0x3c,
    }));

    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->bank, 0U);
    CHECK_EQ(metadata->startEpoch, 1773509970U);
    CHECK_EQ(metadata->endEpoch, 1778136390U);
    CHECK_EQ(metadata->endIndex, 77108U);
    CHECK_EQ(metadata->intervalSeconds, 60U);
}

TEST_CASE("switchbot history stores metadata bank id") {
    const std::uint8_t bank = 0x04;
    const auto metadata = switchbot::history::parseMetadataResponse(bytes({
        0x01,
        0x69, 0xb5, 0x9d, 0x52,
        0x69, 0xfc, 0x35, 0x46,
        0x00, 0x01, 0x2d, 0x34,
        0x00, 0x3c,
    }), bank);

    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->bank, bank);
}

TEST_CASE("switchbot history rejects malformed metadata") {
    CHECK_FALSE(switchbot::history::parseMetadataResponse({}).has_value());
    CHECK_FALSE(switchbot::history::parseMetadataResponse(bytes({0x02, 0, 0, 0, 0})).has_value());
    CHECK_FALSE(switchbot::history::parseMetadataResponse(bytes({
        0x01,
        0x69, 0xb5, 0x9d, 0x52,
        0x69, 0xfc, 0x35, 0x46,
        0x00, 0x01, 0x2d, 0x34,
        0x00, 0x00,
    })).has_value());
}

TEST_CASE("switchbot history decodes captured page") {
    const std::uint32_t firstIndex = 77061;
    const std::uint32_t historyStartEpoch = 1773509973;
    const std::uint16_t interval = 60;

    const auto samples = switchbot::history::decodePageResponse(bytes({
        0x01,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
    }), firstIndex, historyStartEpoch, interval);

    REQUIRE(samples.has_value());
    REQUIRE_EQ(samples->size(), 6U);

    for (std::size_t i = 0; i < samples->size(); ++i) {
        CHECK_EQ((*samples)[i].index, firstIndex + i);
        CHECK_EQ((*samples)[i].epoch, historyStartEpoch + (firstIndex + i) * interval);
        CHECK((*samples)[i].temperatureC == doctest::Approx(11.6f));
        CHECK_EQ((*samples)[i].humidityPct, 50U);
    }
}

TEST_CASE("switchbot history decodes mixed decimals and humidity") {
    const auto samples = switchbot::history::decodePageResponse(bytes({
        0x01,
        0x8b, 0x32, 0x88, 0x8b, 0x32,
        0x8c, 0x31, 0x55, 0x8c, 0x31,
        0x8c, 0x2f, 0x77, 0x8c, 0x2f,
    }), 10, 1000, 60);

    REQUIRE(samples.has_value());
    REQUIRE_EQ(samples->size(), 6U);
    CHECK((*samples)[0].temperatureC == doctest::Approx(11.8f));
    CHECK((*samples)[1].temperatureC == doctest::Approx(11.8f));
    CHECK((*samples)[2].temperatureC == doctest::Approx(12.5f));
    CHECK((*samples)[3].temperatureC == doctest::Approx(12.5f));
    CHECK((*samples)[4].temperatureC == doctest::Approx(12.7f));
    CHECK((*samples)[5].temperatureC == doctest::Approx(12.7f));
    CHECK_EQ((*samples)[2].humidityPct, 49U);
    CHECK_EQ((*samples)[4].humidityPct, 47U);
}

TEST_CASE("switchbot history rejects malformed pages") {
    CHECK_FALSE(switchbot::history::decodePageResponse({}, 0, 0, 60).has_value());
    CHECK_FALSE(switchbot::history::decodePageResponse(bytes({0x02}), 0, 0, 60).has_value());
    CHECK_FALSE(switchbot::history::decodePageResponse(bytes({
        0x01,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
        0x8b, 0x32, 0x66, 0x8b, 0x32,
    }), 0, 0, 0).has_value());
}

TEST_CASE("switchbot history converts epoch and index") {
    const std::uint32_t startEpoch = 1773509975;
    CHECK_EQ(switchbot::history::epochForIndex(startEpoch, 77061, 60), 1778133635U);
    CHECK_EQ(switchbot::history::indexForEpochCeil(startEpoch, 1778133600, 60), 77061U);
    CHECK_EQ(switchbot::history::indexForEpochCeil(startEpoch, 1778133635, 60), 77061U);
    CHECK_EQ(switchbot::history::indexForEpochCeil(startEpoch, 1778133636, 60), 77062U);
    CHECK_EQ(switchbot::history::pageStartForIndex(77061), 77058U);
}
