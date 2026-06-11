#include "payloads.h"

#include <ArduinoJson.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    payload.epochS = *reading.lastSeenEpochS;
    payload.temperatureC = *reading.temperatureC;
    payload.humidityPct = *reading.humidityPct;
    return payload;
}

std::string toJson(const SwitchbotPayload& payload) {
    StaticJsonDocument<256> doc;
    doc["mac"] = payload.mac;
    doc["name"] = payload.name;
    doc["type"] = payload.type;
    doc["timestamp"] = payload.timestamp;
    doc["temperature_c"] = payload.temperatureC;
    doc["humidity_pct"] = static_cast<int>(payload.humidityPct);

    std::string out;
    serializeJson(doc, out);
    return out;
}

namespace {

// --- compact binary helpers ---

void appendU8(std::vector<std::uint8_t>& v, std::uint8_t b) {
    v.push_back(b);
}

void appendI16LE(std::vector<std::uint8_t>& v, std::int16_t val) {
    const auto u = static_cast<std::uint16_t>(val);
    v.push_back(static_cast<std::uint8_t>(u & 0xff));
    v.push_back(static_cast<std::uint8_t>((u >> 8) & 0xff));
}

void appendU32LE(std::vector<std::uint8_t>& v, std::uint32_t val) {
    for (int i = 0; i < 4; ++i) {
        v.push_back(static_cast<std::uint8_t>((val >> (8 * i)) & 0xff));
    }
}

bool parseMacBytes(const std::string& mac, std::uint8_t out[6]) {
    std::string hex;
    hex.reserve(12);
    for (char c : mac) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    if (hex.size() != 12) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        const char hi = hex[2 * i], lo = hex[2 * i + 1];
        const int h = (hi >= '0' && hi <= '9') ? hi - '0' : hi - 'A' + 10;
        const int l = (lo >= '0' && lo <= '9') ? lo - '0' : lo - 'A' + 10;
        out[i] = static_cast<std::uint8_t>((h << 4) | l);
    }
    return true;
}

std::string macBytesToString(const std::uint8_t* bytes) {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                  bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    return buf;
}

bool encodeCompactHeader(std::vector<std::uint8_t>& v, std::uint8_t type,
                         const std::string& mac, std::int64_t epochS,
                         const std::string& name) {
    if (name.size() > 255) {
        return false;
    }
    if (epochS < 0 || epochS > static_cast<std::int64_t>(UINT32_MAX)) {
        return false;
    }
    std::uint8_t macBytes[6];
    if (!parseMacBytes(mac, macBytes)) {
        return false;
    }
    appendU8(v, 1); // compact record version
    appendU8(v, type);
    for (int i = 0; i < 6; ++i) {
        appendU8(v, macBytes[i]);
    }
    appendU32LE(v, static_cast<std::uint32_t>(epochS));
    appendU8(v, static_cast<std::uint8_t>(name.size()));
    for (char c : name) {
        appendU8(v, static_cast<std::uint8_t>(c));
    }
    return true;
}

struct CompactReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t offset = 0;

    bool readU8(std::uint8_t& out) {
        if (offset >= size) return false;
        out = data[offset++];
        return true;
    }

    bool readBytes(std::uint8_t* out, std::size_t n) {
        if (n > size - offset) return false;
        std::memcpy(out, data + offset, n);
        offset += n;
        return true;
    }

    bool readI16LE(std::int16_t& out) {
        if (2 > size - offset) return false;
        const std::uint16_t raw = static_cast<std::uint16_t>(data[offset]) |
                                  (static_cast<std::uint16_t>(data[offset + 1]) << 8);
        out = static_cast<std::int16_t>(raw);
        offset += 2;
        return true;
    }

    bool readU32LE(std::uint32_t& out) {
        if (4 > size - offset) return false;
        out = static_cast<std::uint32_t>(data[offset]) |
              (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
              (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
              (static_cast<std::uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return true;
    }

    bool readString(std::string& out, std::uint8_t len) {
        if (len > size - offset) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
    }

    bool done() const { return offset == size; }
};

bool readCompactHeader(CompactReader& r, std::uint8_t& type,
                       std::string& mac, std::string& timestamp,
                       std::string& name) {
    std::uint8_t version = 0;
    std::uint8_t macBytes[6];
    std::uint32_t epochS = 0;
    std::uint8_t nameLen = 0;

    if (!r.readU8(version) || version != 1) return false;
    if (!r.readU8(type)) return false;
    if (!r.readBytes(macBytes, 6)) return false;
    if (!r.readU32LE(epochS)) return false;
    if (!r.readU8(nameLen)) return false;
    if (!r.readString(name, nameLen)) return false;

    mac = macBytesToString(macBytes);
    const auto ts = timestampString(static_cast<std::int64_t>(epochS));
    if (!ts.has_value()) return false;
    timestamp = *ts;
    return true;
}

} // namespace

std::vector<std::uint8_t> encodeCompact(const SwitchbotPayload& payload) {
    std::vector<std::uint8_t> v;
    if (!encodeCompactHeader(v, 0x01, payload.mac, payload.epochS, payload.name)) {
        return {};
    }
    if (!std::isfinite(payload.temperatureC)) return {};
    const auto tempX10raw = std::round(payload.temperatureC * 10.0f);
    if (tempX10raw < static_cast<float>(INT16_MIN) || tempX10raw > static_cast<float>(INT16_MAX)) return {};
    appendI16LE(v, static_cast<std::int16_t>(tempX10raw));
    appendU8(v, payload.humidityPct);
    return v;
}

bool expandCompact(const char* /*path*/,
                   const std::uint8_t* data, std::size_t size,
                   void* /*context*/,
                   std::string& out) {
    if (size > 0 && data == nullptr) return false;
    CompactReader r{data, size};
    std::uint8_t type = 0;
    std::string mac, timestamp, name;
    if (!readCompactHeader(r, type, mac, timestamp, name)) return false;

    switch (type) {
        case 0x01: { // switchbot
            std::int16_t tempX10 = 0;
            std::uint8_t humidity = 0;
            if (!r.readI16LE(tempX10) || !r.readU8(humidity) || !r.done()) return false;
            SwitchbotPayload payload;
            payload.mac = mac;
            payload.name = name;
            payload.timestamp = timestamp;
            payload.temperatureC = static_cast<float>(tempX10) / 10.0f;
            payload.humidityPct = humidity;
            out = toJson(payload);
            return true;
        }
        default:
            return false;
    }
}

} // namespace api
