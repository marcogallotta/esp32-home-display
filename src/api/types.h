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
    int inMemory = 32;
    std::uint32_t diskReserveBytes = 256 * 1024;
    int drainRateCap = 4;
    int drainRateTickS = 5;
    PqueueLogLevel logLevel = PqueueLogLevel::Info;
};

struct ApiRequest {
    std::string path;
    std::string payload;
};

} // namespace api
