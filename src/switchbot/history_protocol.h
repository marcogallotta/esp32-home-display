#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace switchbot {
namespace history {

constexpr uint8_t kSamplesPerPage = 6;

struct Metadata {
    uint32_t startEpoch = 0;
    uint32_t endEpoch = 0;
    uint32_t endIndex = 0;
    uint16_t intervalSeconds = 0;
};

struct Sample {
    uint32_t index = 0;
    uint32_t epoch = 0;
    double temperatureC = 0.0;
    uint8_t humidityPct = 0;
};

std::vector<uint8_t> buildTimeSyncCommand(uint32_t unixEpoch);
std::vector<uint8_t> buildStartCommand();
std::vector<uint8_t> buildMetadataCommand();
std::vector<uint8_t> buildPageCommand(uint32_t absoluteIndex, uint8_t count = kSamplesPerPage);

std::optional<Metadata> parseMetadataResponse(const std::vector<uint8_t>& response);
std::optional<std::vector<Sample>> decodePageResponse(const std::vector<uint8_t>& response,
                                                       uint32_t pageStartIndex,
                                                       uint32_t startEpoch,
                                                       uint16_t intervalSeconds);

uint32_t indexForEpochFloor(uint32_t startEpoch, uint32_t epoch, uint16_t intervalSeconds);
uint32_t indexForEpochCeil(uint32_t startEpoch, uint32_t epoch, uint16_t intervalSeconds);
uint32_t epochForIndex(uint32_t startEpoch, uint32_t index, uint16_t intervalSeconds);
uint32_t pageStartForIndex(uint32_t index);

// Compatibility aliases for older call sites.
uint32_t epochToIndex(uint32_t epoch, uint32_t startEpoch, uint16_t intervalSeconds);
uint32_t indexToEpoch(uint32_t index, uint32_t startEpoch, uint16_t intervalSeconds);

std::vector<uint8_t> hexToBytes(std::string_view hex);
std::string bytesToHex(const std::vector<uint8_t>& bytes);

}  // namespace history
}  // namespace switchbot
