#pragma once

#include "ble/scanner.h"
#include "xiaomi/protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xiaomi_test {

inline constexpr const char* kFe95Uuid = "0000fe95-0000-1000-8000-00805f9b34fb";

inline constexpr std::uint16_t kTempObjectId         = 0x1004;
inline constexpr std::uint16_t kMoistureObjectId     = 0x1008;
inline constexpr std::uint16_t kLuxObjectId          = 0x1007;
inline constexpr std::uint16_t kConductivityObjectId = 0x1009;

inline std::vector<std::uint8_t> xiaomiPayload(
    std::uint16_t objectId,
    const std::vector<std::uint8_t>& data
) {
    std::vector<std::uint8_t> payload(15, 0x00);
    payload[12] = static_cast<std::uint8_t>(objectId & 0xFF);
    payload[13] = static_cast<std::uint8_t>((objectId >> 8) & 0xFF);
    payload[14] = static_cast<std::uint8_t>(data.size());
    payload.insert(payload.end(), data.begin(), data.end());
    return payload;
}

inline ble::AdvertisementEvent xiaomiAdvertisement(
    const char* address,
    const std::string& uuid,
    const std::vector<std::uint8_t>& payload
) {
    ble::AdvertisementEvent event;
    event.address = address;
    event.serviceData[uuid] = payload;
    return event;
}

} // namespace xiaomi_test
