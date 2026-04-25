#pragma once

#include <string>

#include "../config.h"
#include "../network.h"
#include "payloads.h"
#include "poster.h"

namespace api {

class Client : public ApiPoster {
public:
    explicit Client(const Config& config);

    network::HttpResponse postJson(const std::string& path, const std::string& body) const override;

    network::HttpResponse postSwitchbotReading(const SwitchbotPayload& payload) const;
    network::HttpResponse postXiaomiReading(const XiaomiPayload& payload) const;

private:
    std::string joinUrl(const std::string& path) const;

    const Config& config_;
};

} // namespace api
