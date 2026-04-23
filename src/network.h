#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "config.h"

namespace network {

using Headers = std::map<std::string, std::string>;

enum class Method {
    Get,
    Post,
};

struct Request {
    Method method = Method::Get;
    std::string url;
    std::string body;
    std::string pem;
    std::string contentType = "application/json";
    Headers headers;
};

enum class TransportResult {
    Ok,
    InternalError,
    NetworkError,
    TlsError,
    Timeout,
};

struct HttpResponse {
    TransportResult transport = TransportResult::InternalError;
    int statusCode = 0;
    std::string body;
    std::string error;
};

class Platform {
public:
    explicit Platform(const WifiConfig& wifiConfig)
        : wifiConfig_(wifiConfig) {}
    virtual ~Platform() = default;

    virtual void log(const std::string& msg) = 0;
    virtual uint64_t millis() const = 0;
    virtual bool networkReady(unsigned long timeoutMs = 15000) = 0;

    virtual HttpResponse request(const Request& request) = 0;

    virtual HttpResponse httpGet(const std::string& url, const std::string& pem) {
        Request request;
        request.method = Method::Get;
        request.url = url;
        request.pem = pem;
        return this->request(request);
    }

    virtual HttpResponse httpPost(
        const std::string& url,
        const std::string& body,
        const std::string& pem,
        const std::string& contentType = "application/json",
        const Headers& headers = {}
    ) {
        Request request;
        request.method = Method::Post;
        request.url = url;
        request.body = body;
        request.pem = pem;
        request.contentType = contentType;
        request.headers = headers;
        return this->request(request);
    }

protected:
    const WifiConfig& wifiConfig() const { return wifiConfig_; }

private:
    WifiConfig wifiConfig_;
};

Platform& platform(const WifiConfig& wifiConfig);

} // namespace network
