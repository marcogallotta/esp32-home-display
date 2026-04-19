#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "protocol.h"

namespace xiaomi {

struct SensorReading {
    std::string name;
    std::string shortName;

    bool hasTemperature = false;
    float temperatureC = 0.0f;

    bool hasLux = false;
    int lux = 0;

    bool hasMoisture = false;
    std::uint8_t moisturePct = 0;

    bool hasConductivity = false;
    int conductivityUsCm = 0;

    std::int64_t lastSeenEpochS = 0;
    int rssi = 0;
};

using SensorMap = std::map<std::string, SensorReading>;

class Scanner {
public:
    explicit Scanner(const XiaomiConfig& config);
    ~Scanner();

    void start();
    void stop();
    void poll();

    SensorMap snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xiaomi
