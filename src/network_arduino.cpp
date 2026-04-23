#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "network.h"

namespace network {
namespace {

void configureTlsClient(WiFiClientSecure& client, const std::string& pem, Platform& platform, const std::string& url) {
    if (pem.empty()) {
        platform.log("Warning: empty PEM for " + url + "; TLS verification disabled");
        client.setInsecure();
    } else {
        client.setCACert(pem.c_str());
    }
}

class ArduinoPlatform final : public Platform {
public:
    explicit ArduinoPlatform(const WifiConfig& wifiConfig)
        : Platform(wifiConfig) {}

    void log(const std::string& msg) override {
        Serial.println(msg.c_str());
    }

    uint64_t millis() const override {
        return ::millis();
    }

    bool networkReady(unsigned long timeoutMs = 15000) override {
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }

        WiFi.mode(WIFI_STA);
        WiFi.begin(wifiConfig().ssid.c_str(), wifiConfig().password.c_str());

        const uint32_t start = ::millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (::millis() - start >= timeoutMs) {
                return false;
            }
            delay(250);
        }

        return true;
    }

    HttpResponse request(const Request& request) override {
        return performRequest(request);
    }

private:
    HttpResponse performRequest(const Request& request) {
        HttpResponse resp;

        if (!networkReady(5000)) {
            resp.error = "WiFi not connected";
            return resp;
        }

        WiFiClientSecure client;
        configureTlsClient(client, request.pem, *this, request.url);

        HTTPClient http;
        if (!http.begin(client, request.url.c_str())) {
            resp.error = "http.begin failed";
            return resp;
        }

        http.setTimeout(15000);

        if (request.method == Method::Post) {
            http.addHeader("Content-Type", request.contentType.c_str());
            for (const auto& [key, value] : request.headers) {
                http.addHeader(key.c_str(), value.c_str());
            }
        }

        int code = 0;
        if (request.method == Method::Get) {
            code = http.GET();
        } else {
            auto* data = reinterpret_cast<uint8_t*>(const_cast<char*>(request.body.data()));
            code = http.POST(data, request.body.size());
        }

        resp.statusCode = code;

        if (code > 0) {
            resp.body = http.getString().c_str();
        } else {
            resp.error = http.errorToString(code).c_str();
        }

        http.end();
        return resp;
    }
};

} // namespace

Platform& platform(const WifiConfig& wifiConfig) {
    static ArduinoPlatform instance(wifiConfig);
    return instance;
}

} // namespace network

#endif
