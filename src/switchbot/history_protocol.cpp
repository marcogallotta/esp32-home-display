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
constexpr uint8_t kDefaultPageSampleCount = 6;

constexpr uint8_t kTemperaturePositiveBit = 0x80;
constexpr uint8_t kSevenBitValueMask = 0x7f;
constexpr uint8_t kDecimalNibbleMask = 0x0f;

constexpr size_t kMetadataResponseBytes = 15;
constexpr size_t kMetadataStartEpochOffset = 1;
constexpr size_t kMetadataEndEpochOffset = 5;
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

Sample decodeSample(uint8_t tempByte,
                    uint8_t humidityByte,
                    uint8_t decimalNibble,
                    uint32_t index,
                    uint32_t startEpoch,
                    uint16_t intervalSeconds) {
    const int sign = (tempByte & kTemperaturePositiveBit) ? 1 : -1;
    const int tempInt = tempByte & kSevenBitValueMask;

    Sample sample;
    sample.index = index;
    sample.epoch = indexToEpoch(index, startEpoch, intervalSeconds);
    sample.temperatureC = sign * (tempInt + static_cast<double>(decimalNibble & kDecimalNibbleMask) / 10.0);
    sample.humidityPct = humidityByte & kSevenBitValueMask;
    return sample;
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

std::vector<uint8_t> buildMetadataCommand() {
    std::vector<uint8_t> out = buildHistoryCommand(kMetadataCommand);
    out.push_back(kReservedByte);
    return out;
}

std::vector<uint8_t> buildPageCommand(uint32_t absoluteIndex, uint8_t count) {
    ByteWriter out;
    out.append(kCommandHeader);
    out.append(kHistoryCommandGroup);
    out.append(kPageCommand);
    out.append(kReservedByte);
    out.appendU32BE(absoluteIndex);
    out.append(count == 0 ? kDefaultPageSampleCount : count);
    return out.finish();
}

bool parseMetadataResponse(const std::vector<uint8_t>& response, Metadata& out) {
    if (response.size() != kMetadataResponseBytes || response[0] != kSuccessResponse) {
        return false;
    }

    const ByteReader reader(response);
    out.startEpoch = reader.u32BE(kMetadataStartEpochOffset);
    out.endEpoch = reader.u32BE(kMetadataEndEpochOffset);
    out.endIndex = reader.u32BE(kMetadataEndIndexOffset);
    out.intervalSeconds = reader.u16BE(kMetadataIntervalOffset);
    return out.intervalSeconds != 0;
}

std::vector<Sample> decodePageResponse(const std::vector<uint8_t>& response,
                                       uint32_t pageStartIndex,
                                       uint32_t startEpoch,
                                       uint16_t intervalSeconds) {
    std::vector<Sample> out;
    if (response.size() != kPageResponseBytes || response[0] != kSuccessResponse || intervalSeconds == 0) {
        return out;
    }

    const ByteReader reader(response);
    uint32_t index = pageStartIndex;
    for (size_t group = 0; group < kPackedSampleGroupsPerPage; ++group) {
        const size_t offset = kPagePayloadOffset + group * kPackedSampleGroupBytes;
        const uint8_t temp1 = reader.u8(offset);
        const uint8_t humidity1 = reader.u8(offset + 1);
        const uint8_t decimals = reader.u8(offset + 2);
        const uint8_t temp2 = reader.u8(offset + 3);
        const uint8_t humidity2 = reader.u8(offset + 4);

        out.push_back(decodeSample(temp1, humidity1, decimals >> 4, index++, startEpoch, intervalSeconds));
        out.push_back(decodeSample(temp2, humidity2, decimals & kDecimalNibbleMask, index++, startEpoch, intervalSeconds));
    }
    return out;
}

uint32_t epochToIndex(uint32_t epoch, uint32_t startEpoch, uint16_t intervalSeconds) {
    if (intervalSeconds == 0 || epoch <= startEpoch) {
        return 0;
    }
    return (epoch - startEpoch) / intervalSeconds;
}

uint32_t indexToEpoch(uint32_t index, uint32_t startEpoch, uint16_t intervalSeconds) {
    return startEpoch + index * static_cast<uint32_t>(intervalSeconds);
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
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
        const std::string byte = clean.substr(i, 2);
        out.push_back(static_cast<uint8_t>(std::stoul(byte, nullptr, 16)));
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
