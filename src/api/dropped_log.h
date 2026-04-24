#pragma once

#include <string>

namespace api::dropped_log {

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
);

} // namespace api::dropped_log
