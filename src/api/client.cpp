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

network::HttpResponse Client::postSwitchbotReading(const SwitchbotPayload& payload) const {
    return postJson("/switchbot/reading", toJson(payload));
}

network::HttpResponse Client::postXiaomiReading(const XiaomiPayload& payload) const {
    return postJson("/xiaomi/reading", toJson(payload));
}

network::HttpResponse Client::postJson(const std::string& path, const std::string& body) const {
    auto& p = network::platform(config_.wifi);

    network::Request request;
    request.method = network::Method::Post;
    request.url = joinUrl(path);
    request.body = body;
    request.pem = config_.api.pem;
    request.contentType = "application/json";
    request.headers = {
        {"x-api-key", config_.api.apiKey}
    };

    return p.request(request);
}

std::string Client::joinUrl(const std::string& path) const {
    const std::string base = trimTrailingSlash(config_.api.baseUrl);
    if (!path.empty() && path.front() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

} // namespace api
