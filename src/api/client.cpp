#include "client.h"

#include <ArduinoJson.h>

#include "../network.h"

namespace api {
namespace {

std::string trimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') {
        s.pop_back();
    }
    return s;
}

WriteResponse parseWriteResponse(const network::HttpResponse& r) {
    WriteResponse out;
    out.statusCode = r.statusCode;
    out.body = r.body;
    out.error = r.error;

    StaticJsonDocument<1024> doc;
    const DeserializationError err = deserializeJson(doc, r.body);
    if (err) {
        return out;
    }

    const char* status = doc["status"];
    const char* result = doc["result"];
    if (status == nullptr || result == nullptr) {
        return out;
    }

    if (std::string(status) != "ok") {
        return out;
    }

    const std::string resultStr(result);
    if (resultStr == "created") {
        out.result = WriteResult::Created;
    } else if (resultStr == "duplicate") {
        out.result = WriteResult::Duplicate;
    } else if (resultStr == "merged") {
        out.result = WriteResult::Merged;
    } else if (resultStr == "conflict") {
        out.result = WriteResult::Conflict;
    }

    return out;
}

} // namespace

Client::Client(const Config& config)
    : config_(config) {
}

WriteResponse Client::postSwitchbotReading(const SwitchbotPayload& payload) const {
    return postJson("/switchbot/reading", toJson(payload));
}

WriteResponse Client::postXiaomiReading(const XiaomiPayload& payload) const {
    return postJson("/xiaomi/reading", toJson(payload));
}

WriteResponse Client::postJson(const std::string& path, const std::string& body) const {
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

    const auto r = p.request(request);

    WriteResponse out;
    out.statusCode = r.statusCode;
    out.body = r.body;
    out.error = r.error;

    if (r.transport != network::TransportResult::Ok) {
        p.log(
            "API POST transport failed: error=" + r.error
        );
        return out;
    }

    if (r.statusCode >= 200 && r.statusCode < 300) {
        const WriteResponse parsed = parseWriteResponse(r);
        if (parsed.result == WriteResult::Failed) {
            p.log(
                "API POST returned unrecognized success body: status=" +
                std::to_string(r.statusCode) +
                " body=" + r.body
            );
        }
        return parsed;
    }

    p.log(
        "API POST failed: status=" + std::to_string(r.statusCode) +
        " body=" + r.body
    );

    return out;
}

std::string Client::joinUrl(const std::string& path) const {
    const std::string base = trimTrailingSlash(config_.api.baseUrl);
    if (!path.empty() && path.front() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

} // namespace api
