#include "log.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

#include "platform.h"

namespace {

constexpr bool kUseAnsiColours = true;
bool gLogMuted = false;

struct RateLimitedLogSlot {
    bool used = false;
    LogLevel level = LogLevel::Debug;
    std::string key;
    std::uint64_t nextAllowedMs = 0;
};

constexpr std::size_t kRateLimitedLogSlotCount = 8;
std::array<RateLimitedLogSlot, kRateLimitedLogSlotCount> gRateLimitedLogSlots;

RateLimitedLogSlot* findRateLimitedLogSlot(LogLevel level, const std::string& key) {
    RateLimitedLogSlot* firstUnused = nullptr;

    for (auto& slot : gRateLimitedLogSlots) {
        if (slot.used && slot.level == level && slot.key == key) {
            return &slot;
        }
        if (!slot.used && firstUnused == nullptr) {
            firstUnused = &slot;
        }
    }

    return firstUnused;
}

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

void rateLimitedLog(
    LogLevel level,
    const std::string& key,
    const std::string& msg,
    std::uint64_t intervalMs
) {
    RateLimitedLogSlot* slot = findRateLimitedLogSlot(level, key);
    const std::uint64_t nowMs = platform::millis();

    if (slot == nullptr) {
        logLine(level, msg);
        return;
    }

    if (slot->used && nowMs < slot->nextAllowedMs) {
        return;
    }

    logLine(level, msg);
    slot->used = true;
    slot->level = level;
    slot->key = key;
    slot->nextAllowedMs = nowMs + intervalMs;
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
