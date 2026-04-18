#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "../config.h"
#include "protocol.h"

namespace switchbot {

bool isMeterPayload(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 12) {
        return false;
    }

    bool allZeroAfterMac = true;
    for (std::size_t i = 6; i < payload.size(); ++i) {
        if (payload[i] != 0) {
            allZeroAfterMac = false;
            break;
        }
    }

    return !allZeroAfterMac;
}

std::optional<MeterReading> decodeMeter(
    const std::string& address,
    const std::vector<std::uint8_t>& payload,
    const SwitchbotConfig& config
) {
    if (payload.size() < 11) {
        return std::nullopt;
    }

    const std::uint8_t tempDecimal = payload[8] & 0x0F;
    const std::uint8_t tempIntRaw = payload[9];
    const std::uint8_t humidity = payload[10] & 0x7F;

    const int sign = (tempIntRaw & 0x80) ? 1 : -1;
    const std::uint8_t tempInt = tempIntRaw & 0x7F;
    const float temperature =
        static_cast<float>(sign) *
        (static_cast<float>(tempInt) + static_cast<float>(tempDecimal) / 10.0f);

    auto it = std::find_if(
        config.sensors.begin(),
        config.sensors.end(),
        [&](const SwitchbotSensorConfig& s) {
            return s.mac == address;
        }
    );

    if (it == config.sensors.end()) {
        return std::nullopt;
    }

    return MeterReading{
        address,
        it->name,
        it->shortName,
        temperature,
        humidity,
    };
}

std::vector<std::uint8_t> hexToBytes(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);

    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        const std::string byteStr = hex.substr(i, 2);
        const auto byte = static_cast<std::uint8_t>(std::stoul(byteStr, nullptr, 16));
        out.push_back(byte);
    }

    return out;
}

}  // namespace switchbot
