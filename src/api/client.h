#pragma once

#include <string>

#include "../config.h"
#include "payloads.h"

namespace api {

enum class WriteResult {
    Failed,
    Created,
    Duplicate,
    Merged,
    Conflict,
};

struct WriteResponse {
    WriteResult result = WriteResult::Failed;
    int statusCode = -1;
    std::string body;
    std::string error;
};

class Client {
public:
    explicit Client(const Config& config);

    WriteResponse postSwitchbotReading(const SwitchbotPayload& payload) const;
    WriteResponse postXiaomiReading(const XiaomiPayload& payload) const;

private:
    WriteResponse postJson(const std::string& path, const std::string& body) const;
    std::string joinUrl(const std::string& path) const;

    const Config& config_;
};

} // namespace api
