#include "log.h"

#include <ctime>
#include <string>

#include "platform.h"

namespace {

constexpr bool kUseAnsiColours = true;
bool gLogMuted = false;

const char* logLevelColour(LogLevel level) {
    if (!kUseAnsiColours) {
        return "";
    }

    switch (level) {
        case LogLevel::Debug:
            return "\033[36m"; // cyan
        case LogLevel::Info:
            return "\033[32m"; // green
        case LogLevel::Warn:
            return "\033[33m"; // yellow
        case LogLevel::Error:
            return "\033[31m"; // red
    }

    return "";
}

const char* resetColour() {
    return kUseAnsiColours ? "\033[0m" : "";
}

} // namespace

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

std::string logTimestamp() {
    if (!platform::hasValidTime()) {
        return "t=unknown";
    }

    const std::time_t now = std::time(nullptr);
    std::tm tm{};

    if (localtime_r(&now, &tm) == nullptr) {
        return "t=invalid";
    }

    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return "t=invalid";
    }

    return std::string(buf);
}

void setLogMuted(bool muted) {
    gLogMuted = muted;
}

void logLine(LogLevel level, const std::string& msg) {
    if (gLogMuted) {
        return;
    }

    platform::printLine(
        std::string(logLevelColour(level)) +
        "[" + logTimestamp() + "] " +
        "[" + logLevelName(level) + "] " +
        msg +
        resetColour()
    );
}

std::string transportResultName(network::TransportResult result) {
    switch (result) {
        case network::TransportResult::Ok:
            return "ok";
        case network::TransportResult::InternalError:
            return "internal error";
        case network::TransportResult::NetworkError:
            return "network error";
        case network::TransportResult::TlsError:
            return "TLS error";
        case network::TransportResult::Timeout:
            return "timeout";
    }

    return "unknown";
}
