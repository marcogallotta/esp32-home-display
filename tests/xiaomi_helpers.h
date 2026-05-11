#pragma once

#include "xiaomi/protocol.h"

#include <cstdint>
#include <vector>

namespace xiaomi_test {

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

} // namespace xiaomi_test
