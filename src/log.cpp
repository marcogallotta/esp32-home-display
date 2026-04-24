#include "log.h"

#include "platform.h"

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

void logLine(LogLevel level, const std::string& msg) {
    platform::printLine(std::string("[") + logLevelName(level) + "] " + msg);
}

std::string transportResultName(network::TransportResult result) {
    switch (result) {
        case network::TransportResult::Ok:
            return "ok";
        case network::TransportResult::InternalError:
            return "internal_error";
        case network::TransportResult::NetworkError:
            return "network_error";
        case network::TransportResult::TlsError:
            return "tls_error";
        case network::TransportResult::Timeout:
            return "timeout";
    }

    return "unknown";
}
