#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "../ble/scanner.h"
#include "protocol.h"

namespace xiaomi {

struct SensorReading {
    std::string name;
    std::string shortName;

    bool hasTemperature = false;
    float temperatureC = 0.0f;

    bool hasLux = false;
    std::uint32_t lux = 0;

    bool hasMoisture = false;
    std::uint8_t moisturePct = 0;

    bool hasConductivity = false;
    std::uint16_t conductivityUsCm = 0;

    std::int64_t lastSeenEpochS = 0;
};

using SensorMap = std::map<std::string, SensorReading>;

class Scanner {
public:
    explicit Scanner(const XiaomiConfig& config);
    ~Scanner();

    bool handleAdvertisement(const ble::AdvertisementEvent& event);

    SensorMap snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xiaomi
