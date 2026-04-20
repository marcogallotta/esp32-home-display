#include "payloads.h"

#include <ArduinoJson.h>
#include <ctime>

namespace api {
namespace {

std::optional<std::string> timestampString(const std::optional<std::int64_t>& epochS) {
    if (!epochS.has_value() || *epochS <= 0) {
        return std::nullopt;
    }

    std::time_t t = static_cast<std::time_t>(*epochS);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    if (gmtime_r(&t, &tm) == nullptr) {
        return std::nullopt;
    }
#endif

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return std::nullopt;
    }
    return std::string(buf);
}

std::optional<XiaomiPayload> makeBaseXiaomiPayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    const auto ts = timestampString(reading.lastSeenEpochS);
    if (!ts.has_value()) {
        return std::nullopt;
    }

    XiaomiPayload payload;
    payload.mac = identity.mac;
    payload.name = identity.name;
    payload.timestamp = *ts;
    return payload;
}

} // namespace

std::optional<SwitchbotPayload> makeSwitchbotPayload(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
) {
    if (!reading.temperatureC.has_value() ||
        !reading.humidityPct.has_value()) {
        return std::nullopt;
    }

    const auto ts = timestampString(reading.lastSeenEpochS);
    if (!ts.has_value()) {
        return std::nullopt;
    }

    SwitchbotPayload payload;
    payload.mac = identity.mac;
    payload.name = identity.name;
    payload.timestamp = *ts;
    payload.temperatureC = *reading.temperatureC;
    payload.humidityPct = *reading.humidityPct;
    return payload;
}

std::optional<XiaomiPayload> makeXiaomiTemperaturePayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    if (!reading.temperatureC.has_value()) {
        return std::nullopt;
    }
    auto payload = makeBaseXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        return std::nullopt;
    }
    payload->temperatureC = reading.temperatureC;
    return payload;
}

std::optional<XiaomiPayload> makeXiaomiMoisturePayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    if (!reading.moisturePct.has_value()) {
        return std::nullopt;
    }
    auto payload = makeBaseXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        return std::nullopt;
    }
    payload->moisturePct = reading.moisturePct;
    return payload;
}

std::optional<XiaomiPayload> makeXiaomiLuxPayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    if (!reading.lux.has_value()) {
        return std::nullopt;
    }
    auto payload = makeBaseXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        return std::nullopt;
    }
    payload->lightLux = reading.lux;
    return payload;
}

std::optional<XiaomiPayload> makeXiaomiConductivityPayload(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    if (!reading.conductivityUsCm.has_value()) {
        return std::nullopt;
    }
    auto payload = makeBaseXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        return std::nullopt;
    }
    payload->conductivityUsCm = reading.conductivityUsCm;
    return payload;
}

std::string toJson(const SwitchbotPayload& payload) {
    StaticJsonDocument<256> doc;
    doc["mac"] = payload.mac;
    doc["name"] = payload.name;
    doc["type"] = payload.type;
    doc["timestamp"] = payload.timestamp;
    doc["temperature_c"] = payload.temperatureC;
    doc["humidity_pct"] = payload.humidityPct;

    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string toJson(const XiaomiPayload& payload) {
    StaticJsonDocument<256> doc;
    doc["mac"] = payload.mac;
    doc["name"] = payload.name;
    doc["type"] = payload.type;
    doc["timestamp"] = payload.timestamp;

    if (payload.temperatureC.has_value()) {
        doc["temperature_c"] = *payload.temperatureC;
    } else {
        doc["temperature_c"] = nullptr;
    }

    if (payload.moisturePct.has_value()) {
        doc["moisture_pct"] = *payload.moisturePct;
    } else {
        doc["moisture_pct"] = nullptr;
    }

    if (payload.lightLux.has_value()) {
        doc["light_lux"] = *payload.lightLux;
    } else {
        doc["light_lux"] = nullptr;
    }

    if (payload.conductivityUsCm.has_value()) {
        doc["conductivity_us_cm"] = *payload.conductivityUsCm;
    } else {
        doc["conductivity_us_cm"] = nullptr;
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

} // namespace api
