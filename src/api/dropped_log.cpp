#include "dropped_log.h"

#include <cstdint>
#include <ctime>
#include <string>

#include <ArduinoJson.h>

#include "../log.h"

#ifdef ARDUINO
#include <LittleFS.h>
#else
#include <filesystem>
#include <fstream>
#endif

namespace api::dropped_log {
namespace {

constexpr std::size_t kMaxDroppedLogBytes = 256 * 1024;

#ifdef ARDUINO
constexpr const char* kDroppedLogPath = "/dropped_requests.jsonl";
#else
constexpr const char* kDroppedLogPath = "dropped_requests.jsonl";
#endif

std::string buildLogLine(
    const std::string& reason,
    const std::string& path,
    const std::string& mac,
    const std::string& body,
    int httpStatusCode,
    int transportCode,
    const std::string& error,
    int timeoutRetryCount,
    int tlsRetryCount
) {
    DynamicJsonDocument doc(body.size() + 512);

    doc["ts"] = static_cast<std::int64_t>(std::time(nullptr));
    doc["reason"] = reason;
    doc["path"] = path;
    doc["mac"] = mac;
    doc["http_status_code"] = httpStatusCode;
    doc["transport_code"] = transportCode;
    doc["error"] = error;
    doc["timeout_retry_count"] = timeoutRetryCount;
    doc["tls_retry_count"] = tlsRetryCount;
    doc["body"] = body;

    std::string line;
    serializeJson(doc, line);
    return line;
}

#ifdef ARDUINO
bool ensureFileSystemMounted() {
    static bool attempted = false;
    static bool mounted = false;

    if (!attempted) {
        mounted = LittleFS.begin(false);
        attempted = true;
    }

    return mounted;
}

std::size_t currentLogFileSize() {
    File file = LittleFS.open(kDroppedLogPath, FILE_READ);
    if (!file) {
        return 0;
    }

    const std::size_t size = file.size();
    file.close();
    return size;
}

void appendLine(const std::string& line) {
    if (!ensureFileSystemMounted()) {
        logLine(LogLevel::Warn, "Dropped request log write failed: LittleFS mount failed");
        return;
    }

    const std::size_t currentSize = currentLogFileSize();
    if (currentSize + line.size() + 1 > kMaxDroppedLogBytes) {
        logLine(LogLevel::Warn, "Dropped request log full; discarding record");
        return;
    }

    File file = LittleFS.open(kDroppedLogPath, FILE_APPEND);
    if (!file) {
        logLine(LogLevel::Warn, "Dropped request log write failed: open failed");
        return;
    }

    file.println(line.c_str());
    file.close();
}
#else
std::size_t currentLogFileSize() {
    std::error_code ec;
    if (!std::filesystem::exists(kDroppedLogPath, ec)) {
        return 0;
    }

    return static_cast<std::size_t>(std::filesystem::file_size(kDroppedLogPath, ec));
}

void appendLine(const std::string& line) {
    const std::size_t currentSize = currentLogFileSize();
    if (currentSize + line.size() + 1 > kMaxDroppedLogBytes) {
        logLine(LogLevel::Warn, "Dropped request log full; discarding record");
        return;
    }

    std::ofstream out(kDroppedLogPath, std::ios::app);
    if (!out) {
        logLine(LogLevel::Warn, "Dropped request log write failed: open failed");
        return;
    }

    out << line << '\n';
}
#endif

} // namespace

void appendDroppedRequest(
    const std::string& reason,
    const std::string& path,
    const std::string& mac,
    const std::string& body,
    int httpStatusCode,
    int transportCode,
    const std::string& error,
    int timeoutRetryCount,
    int tlsRetryCount
) {
    appendLine(buildLogLine(
        reason,
        path,
        mac,
        body,
        httpStatusCode,
        transportCode,
        error,
        timeoutRetryCount,
        tlsRetryCount
    ));
}

} // namespace api::dropped_log
