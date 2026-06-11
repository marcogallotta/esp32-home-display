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

    bool hasAnyValue() const;
    bool hasCompleteReading() const;

    bool equalsForDisplay(const SwitchbotReading& other) const;
    bool equalsForApi(const SwitchbotReading& other) const;
};
