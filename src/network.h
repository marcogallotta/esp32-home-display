#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "config.h"

namespace network {

struct HttpResponse {
    int statusCode = -1;
    std::string body;
    std::string error;
};

using Headers = std::map<std::string, std::string>;

class Platform {
public:
    explicit Platform(const WifiConfig& wifiConfig)
        : wifiConfig_(wifiConfig) {}
    virtual ~Platform() = default;

    virtual void log(const std::string& msg) = 0;
    virtual uint64_t millis() const = 0;
    virtual bool networkReady(unsigned long timeoutMs = 15000) = 0;

    virtual HttpResponse httpGet(const std::string& url, const std::string& pem) = 0;
    virtual HttpResponse httpPost(
        const std::string& url,
        const std::string& body,
        const std::string& pem,
        const std::string& contentType = "application/json",
        const Headers& headers = {}
    ) = 0;

protected:
    const WifiConfig& wifiConfig() const { return wifiConfig_; }

private:
    WifiConfig wifiConfig_;
};

Platform& platform(const WifiConfig& wifiConfig);

} // namespace network
