#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../config.h"

namespace switchbot {

struct MeterReading {
    std::string address;
    std::string name;
    std::string shortName;
    float temperature_c;
    std::uint8_t humidity;
};

bool isMeterPayload(const std::vector<std::uint8_t>& payload);

std::optional<MeterReading> decodeMeter(
    const std::string& address,
    const std::vector<std::uint8_t>& payload,
    const SwitchbotConfig& config
);

std::vector<std::uint8_t> hexToBytes(const std::string& hex);

}  // namespace switchbot
