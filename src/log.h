#pragma once

#include <cstdint>
#include <string>

#include "network.h"

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

const char* logLevelName(LogLevel level);
void setLogMuted(bool muted);
void logLine(LogLevel level, const std::string& msg);
void rateLimitedLog(
    LogLevel level,
    const std::string& key,
    const std::string& msg,
    std::uint64_t intervalMs
);

std::string transportResultName(network::TransportResult result);
