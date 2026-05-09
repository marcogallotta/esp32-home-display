#pragma once

#include <cstdint>
#include <string>

#include "network.h"

#define APP_LOG_LEVEL_DEBUG 0
#define APP_LOG_LEVEL_INFO 1
#define APP_LOG_LEVEL_WARN 2
#define APP_LOG_LEVEL_ERROR 3

#ifndef LOG_LEVEL
#define LOG_LEVEL APP_LOG_LEVEL_INFO
#endif

#if LOG_LEVEL < APP_LOG_LEVEL_DEBUG || LOG_LEVEL > APP_LOG_LEVEL_ERROR
#error "LOG_LEVEL must be one of APP_LOG_LEVEL_DEBUG, APP_LOG_LEVEL_INFO, APP_LOG_LEVEL_WARN, APP_LOG_LEVEL_ERROR"
#endif

enum class LogLevel {
    Debug = APP_LOG_LEVEL_DEBUG,
    Info = APP_LOG_LEVEL_INFO,
    Warn = APP_LOG_LEVEL_WARN,
    Error = APP_LOG_LEVEL_ERROR,
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
