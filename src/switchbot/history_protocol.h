#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace switchbot {
namespace history {

constexpr uint8_t kSamplesPerPage = 6;

struct StartResponse {
    uint8_t status = 0;
    std::vector<uint8_t> banks;
};

struct Metadata {
    uint8_t bank = 0;
    uint32_t startEpoch = 0;
    uint32_t endIndex = 0;
    uint16_t intervalSeconds = 0;
};


struct HistoryBank {
    uint8_t id = 0;
    Metadata metadata;
};

struct Sample {
    uint32_t index = 0;
    uint32_t epoch = 0;
    double temperatureC = 0.0;
    uint8_t humidityPct = 0;
};

std::vector<uint8_t> buildTimeSyncCommand(uint32_t unixEpoch);
std::vector<uint8_t> buildStartCommand();
std::vector<uint8_t> buildBankMetadataCommand(uint8_t bank);
std::vector<uint8_t> buildBankPageCommand(uint8_t bank, uint32_t bankLocalIndex, uint8_t count = kSamplesPerPage);

std::optional<StartResponse> parseStartResponse(const std::vector<uint8_t>& response);
std::optional<Metadata> parseMetadataResponse(const std::vector<uint8_t>& response, uint8_t bank = 0);
std::optional<std::vector<Sample>> decodePageResponse(const std::vector<uint8_t>& response,
                                                       uint32_t pageStartIndex,
                                                       uint32_t startEpoch,
                                                       uint16_t intervalSeconds);

uint32_t indexForEpochCeil(uint32_t startEpoch, uint32_t epoch, uint16_t intervalSeconds);
uint32_t epochForIndex(uint32_t startEpoch, uint32_t index, uint16_t intervalSeconds);
uint32_t pageStartForIndex(uint32_t index);

std::vector<uint8_t> hexToBytes(std::string_view hex);
std::string bytesToHex(const std::vector<uint8_t>& bytes);

}  // namespace history
}  // namespace switchbot
