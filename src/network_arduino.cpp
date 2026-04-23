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
        if (request.method == Method::Get) {
            return performGet(request.url, request.pem);
        }

        return performPost(
            request.url,
            request.body,
            request.pem,
            request.contentType,
            request.headers
        );
    }

private:
    HttpResponse performGet(const std::string& url, const std::string& pem) {
        HttpResponse resp;

        if (!networkReady(5000)) {
            resp.error = "WiFi not connected";
            return resp;
        }

        WiFiClientSecure client;
        configureTlsClient(client, pem, *this, url);

        HTTPClient http;
        if (!http.begin(client, url.c_str())) {
            resp.error = "http.begin failed";
            return resp;
        }

        http.setTimeout(15000);

        const int code = http.GET();
        resp.statusCode = code;

        if (code > 0) {
            resp.body = http.getString().c_str();
        } else {
            resp.error = http.errorToString(code).c_str();
        }

        http.end();
        return resp;
    }

    HttpResponse performPost(
        const std::string& url,
        const std::string& body,
        const std::string& pem,
        const std::string& contentType,
        const Headers& headers
    ) {
        HttpResponse resp;

        if (!networkReady(5000)) {
            resp.error = "WiFi not connected";
            return resp;
        }

        WiFiClientSecure client;
        configureTlsClient(client, pem, *this, url);

        HTTPClient http;
        if (!http.begin(client, url.c_str())) {
            resp.error = "http.begin failed";
            return resp;
        }

        http.setTimeout(15000);
        http.addHeader("Content-Type", contentType.c_str());
        for (const auto& [key, value] : headers) {
            http.addHeader(key.c_str(), value.c_str());
        }

        auto* data = reinterpret_cast<uint8_t*>(const_cast<char*>(body.data()));
        const int code = http.POST(data, body.size());
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
