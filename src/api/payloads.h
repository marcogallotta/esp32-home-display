#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../sensor_readings.h"

namespace api {

struct SwitchbotPayload {
    std::string mac;
    std::string name;
    std::string type = "switchbot";
    std::string timestamp;
    float temperatureC = 0.0f;
    std::uint8_t humidityPct = 0;
};

struct XiaomiPayload {
    std::string mac;
    std::string name;
    std::string type = "xiaomi";
    std::string timestamp;

    std::optional<float> temperatureC;
    std::optional<std::uint8_t> moisturePct;
    std::optional<int> lightLux;
    std::optional<int> conductivityUsCm;
};

std::optional<SwitchbotPayload> makeSwitchbotPayload(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
);

std::optional<XiaomiPayload> makeXiaomiTemperaturePayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
);

std::optional<XiaomiPayload> makeXiaomiMoisturePayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
);

std::optional<XiaomiPayload> makeXiaomiLuxPayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
);

std::optional<XiaomiPayload> makeXiaomiConductivityPayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
);

std::string toJson(const SwitchbotPayload& payload);
std::string toJson(const XiaomiPayload& payload);

} // namespace api
