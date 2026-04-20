#pragma once

#include "payloads.h"
#include "../config.h"

namespace api {

class Client {
public:
    explicit Client(const Config& config);

    bool postSwitchbotReading(const SwitchbotPayload& payload) const;
    bool postXiaomiReading(const XiaomiPayload& payload) const;

private:
    bool postJson(const std::string& path, const std::string& body) const;
    std::string joinUrl(const std::string& path) const;

    const Config& config_;
};

} // namespace api
