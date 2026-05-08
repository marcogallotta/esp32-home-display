#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../config.h"
#include "protocol.h"

namespace switchbot {
namespace {

constexpr std::size_t kMacBytesInManufacturerPayload = 6;
constexpr std::size_t kTemperatureDecimalOffset = 8;
constexpr std::size_t kTemperatureIntegerOffset = 9;
constexpr std::size_t kHumidityOffset = 10;
constexpr std::size_t kMinimumMeterPayloadBytes = 12;

constexpr std::uint8_t kPositiveTemperatureBit = 0x80;
constexpr std::uint8_t kSevenBitValueMask = 0x7f;
constexpr std::uint8_t kDecimalNibbleMask = 0x0f;

bool hasEnoughBytesForMeterPayload(const std::vector<std::uint8_t>& payload) {
    return payload.size() >= kMinimumMeterPayloadBytes;
}

bool hasAnyDataAfterMac(const std::vector<std::uint8_t>& payload) {
    for (std::size_t i = kMacBytesInManufacturerPayload; i < payload.size(); ++i) {
        if (payload[i] != 0) {
            return true;
        }
    }
    return false;
}

float decodeTemperatureC(std::uint8_t decimalByte, std::uint8_t integerByte) {
    const int sign = (integerByte & kPositiveTemperatureBit) ? 1 : -1;
    const std::uint8_t wholeDegrees = integerByte & kSevenBitValueMask;
    const std::uint8_t decimalDegrees = decimalByte & kDecimalNibbleMask;
    return static_cast<float>(sign) *
        (static_cast<float>(wholeDegrees) + static_cast<float>(decimalDegrees) / 10.0f);
}

std::uint8_t decodeHumidityPct(std::uint8_t humidityByte) {
    return humidityByte & kSevenBitValueMask;
}

const SwitchbotSensorConfig* findConfiguredSensor(const std::string& address,
                                                  const SwitchbotConfig& config) {
    const auto it = std::find_if(
        config.sensors.begin(),
        config.sensors.end(),
        [&](const SwitchbotSensorConfig& sensor) {
            return sensor.mac == address;
        }
    );

    return it == config.sensors.end() ? nullptr : &(*it);
}

}  // namespace

bool isMeterPayload(const std::vector<std::uint8_t>& payload) {
    return hasEnoughBytesForMeterPayload(payload) && hasAnyDataAfterMac(payload);
}

std::optional<MeterReading> decodeMeter(
    const std::string& address,
    const std::vector<std::uint8_t>& payload,
    const SwitchbotConfig& config
) {
    if (!hasEnoughBytesForMeterPayload(payload)) {
        return std::nullopt;
    }

    const SwitchbotSensorConfig* sensor = findConfiguredSensor(address, config);
    if (sensor == nullptr) {
        return std::nullopt;
    }

    return MeterReading{
        address,
        sensor->name,
        sensor->shortName,
        decodeTemperatureC(payload[kTemperatureDecimalOffset], payload[kTemperatureIntegerOffset]),
        decodeHumidityPct(payload[kHumidityOffset]),
    };
}

}  // namespace switchbot
