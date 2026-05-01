#pragma once

#include <cstdint>
#include <string>

namespace api {

struct BufferConfig {
    int inMemory = 32;
    std::uint32_t diskReserveBytes = 256 * 1024;
    int drainRateCap = 4;
    int drainRateTickS = 5;
};

struct BufferRecord {
    std::string path;
    std::string payload;
    int timeoutRetryCount = 0;
    int tlsRetryCount = 0;
};

using ApiRequest = BufferRecord;

} // namespace api
