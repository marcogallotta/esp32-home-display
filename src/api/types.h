#pragma once

#include <cstdint>
#include <string>

namespace api {

enum class PqueueLogLevel {
    Debug,
    Info,
    Warning,
    Error,
    None,
};

struct OutboxConfig {
    int inMemory = 16;
    std::uint32_t diskReserveBytes = 128 * 1024;
    int drainRateCap = 5;
    int drainRateTickS = 1;
    std::uint32_t retryDelayMs = 10000;
    PqueueLogLevel logLevel = PqueueLogLevel::Info;
};

struct ApiRequest {
    std::string path;
    std::string payload;
};

} // namespace api
