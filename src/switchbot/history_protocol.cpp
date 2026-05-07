#include "switchbot/history_protocol.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace switchbot {
namespace history {
namespace {

void appendU32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(value & 0xff));
}

uint32_t readU32BE(const std::vector<uint8_t>& bytes, size_t offset) {
    return (static_cast<uint32_t>(bytes[offset]) << 24) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<uint32_t>(bytes[offset + 3]);
}

uint16_t readU16BE(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
                                 static_cast<uint16_t>(bytes[offset + 1]));
}

Sample decodeSample(uint8_t tempByte,
                    uint8_t humidityByte,
                    uint8_t decimalNibble,
                    uint32_t index,
                    uint32_t startEpoch,
                    uint16_t intervalSeconds) {
    const int sign = (tempByte & 0x80) ? 1 : -1;
    const int tempInt = tempByte & 0x7f;
    Sample sample;
    sample.index = index;
    sample.epoch = indexToEpoch(index, startEpoch, intervalSeconds);
    sample.temperatureC = sign * (tempInt + static_cast<double>(decimalNibble & 0x0f) / 10.0);
    sample.humidityPct = humidityByte & 0x7f;
    return sample;
}

}  // namespace

std::vector<uint8_t> buildTimeSyncCommand(uint32_t unixEpoch) {
    std::vector<uint8_t> out = {0x57, 0x00, 0x05, 0x03, 0x0d, 0x00, 0x00, 0x00, 0x00};
    appendU32BE(out, unixEpoch);
    return out;
}

std::vector<uint8_t> buildStartCommand() {
    return {0x57, 0x0f, 0x3a};
}

std::vector<uint8_t> buildMetadataCommand() {
    return {0x57, 0x0f, 0x3b, 0x00};
}

std::vector<uint8_t> buildPageCommand(uint32_t absoluteIndex, uint8_t count) {
    std::vector<uint8_t> out = {0x57, 0x0f, 0x3c, 0x00};
    appendU32BE(out, absoluteIndex);
    out.push_back(count);
    return out;
}

bool parseMetadataResponse(const std::vector<uint8_t>& response, Metadata& out) {
    if (response.size() != 16 || response[0] != 0x01) {
        return false;
    }
    out.startEpoch = readU32BE(response, 1);
    out.endEpoch = readU32BE(response, 5);
    out.endIndex = readU32BE(response, 9);
    out.intervalSeconds = readU16BE(response, 13);
    return out.intervalSeconds != 0;
}

std::vector<Sample> decodePageResponse(const std::vector<uint8_t>& response,
                                       uint32_t pageStartIndex,
                                       uint32_t startEpoch,
                                       uint16_t intervalSeconds) {
    std::vector<Sample> out;
    if (response.size() != 16 || response[0] != 0x01 || intervalSeconds == 0) {
        return out;
    }

    size_t offset = 1;
    uint32_t index = pageStartIndex;
    for (int group = 0; group < 3; ++group) {
        const uint8_t temp1 = response[offset];
        const uint8_t humidity1 = response[offset + 1];
        const uint8_t decimals = response[offset + 2];
        const uint8_t temp2 = response[offset + 3];
        const uint8_t humidity2 = response[offset + 4];
        out.push_back(decodeSample(temp1, humidity1, decimals >> 4, index++, startEpoch, intervalSeconds));
        out.push_back(decodeSample(temp2, humidity2, decimals & 0x0f, index++, startEpoch, intervalSeconds));
        offset += 5;
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
