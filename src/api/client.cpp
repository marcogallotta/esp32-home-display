#include "client.h"

#include "../network.h"

namespace api {
namespace {

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

} // namespace

Client::Client(const Config& config)
    : config_(config) {
}

bool Client::postSwitchbotReading(const SwitchbotPayload& payload) const {
    return postJson("/switchbot/readings", toJson(payload));
}

bool Client::postXiaomiReading(const XiaomiPayload& payload) const {
    return postJson("/xiaomi/readings", toJson(payload));
}

bool Client::postJson(const std::string& path, const std::string& body) const {
    auto& p = network::platform(config_.wifi);
    const auto r = p.httpPost(joinUrl(path), body, config_.api.pem, "application/json");
    if (r.statusCode >= 200 && r.statusCode < 300) {
        return true;
    }

    p.log(
        "API POST failed: status=" + std::to_string(r.statusCode) +
        " error=" + r.error +
        " body=" + r.body
    );
    return false;
}

std::string Client::joinUrl(const std::string& path) const {
    const std::string base = trimTrailingSlash(config_.api.baseUrl);
    if (!path.empty() && path.front() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

} // namespace api
