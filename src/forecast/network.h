#pragma once

#include <cstdint>
#include <string>

#include "../config.h"

namespace forecast {

struct HttpResponse {
    int status_code = -1;
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

    // Ensure network is usable. On desktop this may just return true.
    virtual bool networkReady(unsigned long timeoutMs = 15000) = 0;

    // HTTPS GET preferred. URL should be full URL.
    virtual HttpResponse httpGet(const std::string& url, const std::string& pem) = 0;

protected:
    const WifiConfig& wifiConfig() const { return wifiConfig_; }

private:
    WifiConfig wifiConfig_;
};

Platform& platform(const WifiConfig& wifiConfig);

} // namespace forecast
