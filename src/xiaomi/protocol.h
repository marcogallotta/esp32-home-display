#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../config.h"

namespace xiaomi {

struct DecodedObject {
    enum class Kind {
        Temperature,
        Lux,
        Moisture,
        Conductivity,
    };

    Kind kind;
    float temperatureC = 0.0f;
    std::uint32_t lux = 0;
    std::uint8_t moisturePct = 0;
    std::uint16_t conductivityUsCm = 0;
};

bool isXiaomiServiceDataUuid(const std::string& uuid);

std::optional<DecodedObject> decodeObject(
    const std::vector<std::uint8_t>& payload
);

const XiaomiSensorConfig* findSensorConfig(
    const XiaomiConfig& config,
    const std::string& address
);

} // namespace xiaomi
