#pragma once

#include <cstdint>
#include <string>

namespace pqueue {

struct Config {
    int inMemory = 32;
    std::uint32_t diskReserveBytes = 256 * 1024;
    int drainRateCap = 4;
    int drainRateTickS = 5;
};

struct Record {
    std::string path;
    std::string body;
    int timeoutRetryCount = 0;
    int tlsRetryCount = 0;
};

} // namespace pqueue
