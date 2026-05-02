#pragma once

#include <cstdint>
#include <string>

namespace api {

struct OutboxConfig {
    int inMemory = 32;
    std::uint32_t diskReserveBytes = 256 * 1024;
    int drainRateCap = 4;
    int drainRateTickS = 5;
};

struct ApiRequest {
    std::string path;
    std::string payload;
};

} // namespace api
