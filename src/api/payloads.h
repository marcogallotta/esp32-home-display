#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../sensor_readings.h"

namespace api {

struct SwitchbotPayload {
    std::string mac;
    std::string name;
    std::string type = "switchbot";
    std::string timestamp;
    std::int64_t epochS = 0;
    float temperatureC = 0.0f;
    std::uint8_t humidityPct = 0;
};

std::optional<SwitchbotPayload> makeSwitchbotPayload(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
);

std::string toJson(const SwitchbotPayload& payload);

// Compact binary encoding. Returns an empty vector on failure.
std::vector<std::uint8_t> encodeCompact(const SwitchbotPayload& payload);

// Reconstructs JSON from a compact binary record. Matches ExpandBodyCallback.
// Returns true and populates out on success; false on any decode error.
bool expandCompact(const char* path,
                   const std::uint8_t* data, std::size_t size,
                   void* context,
                   std::string& out);

} // namespace api
