#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct SensorIdentity {
    std::string mac;
    std::string name;
    std::string shortName;
};

class SwitchbotReading {
public:
    std::optional<float> temperatureC;
    std::optional<std::uint8_t> humidityPct;
    std::optional<std::int64_t> lastSeenEpochS;
    std::optional<int> rssi;

    bool hasAnyValue() const;
    bool hasCompleteReading() const;

    bool equalsForDisplay(const SwitchbotReading& other) const;
    bool equalsForApi(const SwitchbotReading& other) const;
};

class XiaomiReading {
public:
    std::optional<float> temperatureC;
    std::optional<std::uint8_t> moisturePct;
    std::optional<int> lux;
    std::optional<int> conductivityUsCm;
    std::optional<std::int64_t> lastSeenEpochS;
    std::optional<int> rssi;

    bool hasAnyValue() const;

    bool equalsForDisplay(const XiaomiReading& other) const;
    bool equalsForApi(const XiaomiReading& other) const;
};
