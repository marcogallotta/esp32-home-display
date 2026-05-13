#include "switchbot/history_protocol.h"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace switchbot {
namespace history {
namespace {

constexpr uint8_t kSuccessResponse = 0x01;
constexpr uint8_t kCommandHeader = 0x57;
constexpr uint8_t kHistoryCommandGroup = 0x0f;
constexpr uint8_t kReservedByte = 0x00;

constexpr uint8_t kTemperaturePositiveBit = 0x80;
constexpr uint8_t kSevenBitValueMask = 0x7f;
constexpr uint8_t kDecimalNibbleMask = 0x0f;

constexpr size_t kMetadataResponseBytes = 15;
constexpr size_t kMetadataStartEpochOffset = 1;
constexpr size_t kMetadataEndIndexOffset = 9;
constexpr size_t kMetadataIntervalOffset = 13;

constexpr size_t kPageResponseBytes = 16;
constexpr size_t kPagePayloadOffset = 1;
constexpr size_t kPackedSampleGroupBytes = 5;
constexpr size_t kPackedSampleGroupsPerPage = 3;

constexpr uint8_t kStartHistoryCommand = 0x3a;
constexpr uint8_t kMetadataCommand = 0x3b;
constexpr uint8_t kPageCommand = 0x3c;

const uint8_t kTimeSyncPrefix[] = {
    kCommandHeader, 0x00, 0x05, 0x03, 0x0d,
    kReservedByte, kReservedByte, kReservedByte, kReservedByte,
};

struct PackedSampleGroup {
    uint8_t firstTemperature;
    uint8_t firstHumidity;
    uint8_t decimalNibbles;
    uint8_t secondTemperature;
    uint8_t secondHumidity;
};

class ByteWriter {
public:
    void append(uint8_t value) { bytes_.push_back(value); }

    void appendBytes(const uint8_t* values, size_t count) {
        bytes_.insert(bytes_.end(), values, values + count);
    }

    void appendU32BE(uint32_t value) {
        append(static_cast<uint8_t>((value >> 24) & 0xff));
        append(static_cast<uint8_t>((value >> 16) & 0xff));
        append(static_cast<uint8_t>((value >> 8) & 0xff));
        append(static_cast<uint8_t>(value & 0xff));
    }

    std::vector<uint8_t> finish() { return std::move(bytes_); }

private:
    std::vector<uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<uint8_t>& bytes)
        : bytes_(bytes) {}

    uint8_t u8(size_t offset) const {
        return bytes_[offset];
    }

    uint16_t u16BE(size_t offset) const {
        return static_cast<uint16_t>((static_cast<uint16_t>(bytes_[offset]) << 8) |
                                     static_cast<uint16_t>(bytes_[offset + 1]));
    }

    uint32_t u32BE(size_t offset) const {
        return (static_cast<uint32_t>(bytes_[offset]) << 24) |
               (static_cast<uint32_t>(bytes_[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes_[offset + 2]) << 8) |
               static_cast<uint32_t>(bytes_[offset + 3]);
    }

private:
    const std::vector<uint8_t>& bytes_;
};

std::vector<uint8_t> buildHistoryCommand(uint8_t command) {
    return {kCommandHeader, kHistoryCommandGroup, command};
}

bool isSuccessResponse(const std::vector<uint8_t>& response, size_t expectedSize) {
    return response.size() == expectedSize && response[0] == kSuccessResponse;
}

PackedSampleGroup readPackedSampleGroup(const ByteReader& reader, size_t groupNumber) {
    const size_t offset = kPagePayloadOffset + groupNumber * kPackedSampleGroupBytes;
    return PackedSampleGroup{
        reader.u8(offset),
        reader.u8(offset + 1),
        reader.u8(offset + 2),
        reader.u8(offset + 3),
        reader.u8(offset + 4),
    };
}

double decodeTemperatureC(uint8_t temperatureByte, uint8_t decimalNibble) {
    const int sign = (temperatureByte & kTemperaturePositiveBit) ? 1 : -1;
    const int wholeDegrees = temperatureByte & kSevenBitValueMask;
    const double decimalDegrees = static_cast<double>(decimalNibble & kDecimalNibbleMask) / 10.0;
    return sign * (wholeDegrees + decimalDegrees);
}

uint8_t decodeHumidityPct(uint8_t humidityByte) {
    return humidityByte & kSevenBitValueMask;
}

Sample decodeSample(uint8_t temperatureByte,
                    uint8_t humidityByte,
                    uint8_t decimalNibble,
                    uint32_t index,
                    uint32_t startEpoch,
                    uint16_t intervalSeconds) {
    return Sample{
        index,
        epochForIndex(startEpoch, index, intervalSeconds),
        decodeTemperatureC(temperatureByte, decimalNibble),
        decodeHumidityPct(humidityByte),
    };
}

void appendDecodedGroup(std::vector<Sample>& out,
                        const PackedSampleGroup& group,
                        uint32_t& nextIndex,
                        uint32_t startEpoch,
                        uint16_t intervalSeconds) {
    out.push_back(decodeSample(
        group.firstTemperature,
        group.firstHumidity,
        group.decimalNibbles >> 4,
        nextIndex++,
        startEpoch,
        intervalSeconds
    ));
    out.push_back(decodeSample(
        group.secondTemperature,
        group.secondHumidity,
        group.decimalNibbles & kDecimalNibbleMask,
        nextIndex++,
        startEpoch,
        intervalSeconds
    ));
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::vector<uint8_t> buildTimeSyncCommand(uint32_t unixEpoch) {
    ByteWriter out;
    out.appendBytes(kTimeSyncPrefix, sizeof(kTimeSyncPrefix));
    out.appendU32BE(unixEpoch);
    return out.finish();
}

std::vector<uint8_t> buildStartCommand() {
    return buildHistoryCommand(kStartHistoryCommand);
}

std::vector<uint8_t> buildBankMetadataCommand(uint8_t bank) {
    std::vector<uint8_t> out = buildHistoryCommand(kMetadataCommand);
    out.push_back(bank);
    return out;
}

std::vector<uint8_t> buildBankPageCommand(uint8_t bank, uint32_t bankLocalIndex, uint8_t count) {
    ByteWriter out;
    out.append(kCommandHeader);
    out.append(kHistoryCommandGroup);
    out.append(kPageCommand);
    out.append(bank);
    out.appendU32BE(bankLocalIndex);
    out.append(count == 0 ? kSamplesPerPage : count);
    return out.finish();
}

std::optional<StartResponse> parseStartResponse(const std::vector<uint8_t>& response) {
    if (response.size() < 3 || response[0] != kSuccessResponse) {
        return std::nullopt;
    }

    StartResponse start;
    start.status = response[1];
    start.banks.assign(response.begin() + 2, response.end());
    return start;
}

std::optional<Metadata> parseMetadataResponse(const std::vector<uint8_t>& response, uint8_t bank) {
    if (!isSuccessResponse(response, kMetadataResponseBytes)) {
        return std::nullopt;
    }

    const ByteReader reader(response);
    Metadata metadata;
    metadata.bank = bank;
    metadata.startEpoch = reader.u32BE(kMetadataStartEpochOffset);
    metadata.endIndex = reader.u32BE(kMetadataEndIndexOffset);
    metadata.intervalSeconds = reader.u16BE(kMetadataIntervalOffset);

    if (metadata.intervalSeconds == 0) {
        return std::nullopt;
    }
    return metadata;
}

std::optional<std::vector<Sample>> decodePageResponse(const std::vector<uint8_t>& response,
                                                       uint32_t pageStartIndex,
                                                       uint32_t startEpoch,
                                                       uint16_t intervalSeconds) {
    if (!isSuccessResponse(response, kPageResponseBytes) || intervalSeconds == 0) {
        return std::nullopt;
    }

    const ByteReader reader(response);
    std::vector<Sample> out;
    out.reserve(kSamplesPerPage);

    uint32_t nextIndex = pageStartIndex;
    for (size_t group = 0; group < kPackedSampleGroupsPerPage; ++group) {
        appendDecodedGroup(out, readPackedSampleGroup(reader, group), nextIndex, startEpoch, intervalSeconds);
    }
    return out;
}

uint32_t indexForEpochCeil(uint32_t startEpoch, uint32_t epoch, uint16_t intervalSeconds) {
    if (intervalSeconds == 0 || epoch <= startEpoch) {
        return 0;
    }

    const uint32_t delta = epoch - startEpoch;
    return (delta + static_cast<uint32_t>(intervalSeconds) - 1) /
        static_cast<uint32_t>(intervalSeconds);
}

uint32_t epochForIndex(uint32_t startEpoch, uint32_t index, uint16_t intervalSeconds) {
    return startEpoch + index * static_cast<uint32_t>(intervalSeconds);
}

uint32_t pageStartForIndex(uint32_t index) {
    return index - (index % kSamplesPerPage);
}


std::vector<uint8_t> hexToBytes(std::string_view hex) {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            clean.push_back(c);
        }
    }
    if (clean.size() % 2 != 0) {
        throw std::invalid_argument("hex string must contain an even number of digits");
    }

    std::vector<uint8_t> out;
    out.reserve(clean.size() / 2);
    for (size_t i = 0; i < clean.size(); i += 2) {
        const int high = hexValue(clean[i]);
        const int low = hexValue(clean[i + 1]);
        if (high < 0 || low < 0) {
            throw std::invalid_argument("hex string contains a non-hex digit");
        }
        out.push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return out;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint8_t b : bytes) {
        out << std::setw(2) << static_cast<int>(b);
    }
    return out.str();
}

}  // namespace history
}  // namespace switchbot
