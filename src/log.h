#pragma once

#include <string>

#include "network.h"

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

const char* logLevelName(LogLevel level);
void logLine(LogLevel level, const std::string& msg);

std::string transportResultName(network::TransportResult result);
