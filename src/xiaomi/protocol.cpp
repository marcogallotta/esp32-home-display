#include "protocol.h"

#include <algorithm>
#include <cctype>

namespace xiaomi {

namespace {
constexpr const char* kFe95Uuid = "0000fe95-0000-1000-8000-00805f9b34fb";
}

bool isXiaomiServiceDataUuid(const std::string& uuid) {
    std::string normalized = uuid;
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return normalized == kFe95Uuid;
}

std::optional<DecodedObject> decodeObject(const std::vector<std::uint8_t>& payload) {
    if (payload.size() < 15) {
        return std::nullopt;
    }

    const std::uint16_t objId =
        static_cast<std::uint16_t>(payload[12] | (payload[13] << 8));
    const std::size_t length = payload[14];

    if (payload.size() < 15 + length) {
        return std::nullopt;
    }

    const std::uint8_t* data = payload.data() + 15;

    switch (objId) {
        case 0x1004:
            if (length != 2) {
                return std::nullopt;
            }
            return DecodedObject{
                DecodedObject::Kind::Temperature,
                static_cast<float>(
                    static_cast<std::int16_t>(data[0] | (data[1] << 8))
                ) / 10.0f,
                0,
                0,
                0,
            };

        case 0x1007:
            if (length != 3) {
                return std::nullopt;
            }
            return DecodedObject{
                DecodedObject::Kind::Lux,
                0.0f,
                static_cast<int>(data[0] | (data[1] << 8) | (data[2] << 16)),
                0,
                0,
            };

        case 0x1008:
            if (length != 1) {
                return std::nullopt;
            }
            return DecodedObject{
                DecodedObject::Kind::Moisture,
                0.0f,
                0,
                data[0],
                0,
            };

        case 0x1009:
            if (length != 2) {
                return std::nullopt;
            }
            return DecodedObject{
                DecodedObject::Kind::Conductivity,
                0.0f,
                0,
                0,
                static_cast<int>(data[0] | (data[1] << 8)),
            };

        default:
            return std::nullopt;
    }
}

const XiaomiSensorConfig* findSensorConfig(
    const XiaomiConfig& config,
    const std::string& address
) {
    for (const auto& sensor : config.sensors) {
        if (sensor.mac == address) {
            return &sensor;
        }
    }
    return nullptr;
}

} // namespace xiaomi
