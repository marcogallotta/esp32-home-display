#ifdef ARDUINO

#include <Arduino.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../config.h"
#include "network.h"

namespace forecast {

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

        return WiFi.status() == WL_CONNECTED;
    }

    HttpResponse httpGet(const std::string& url, const std::string& pem) override {
        HttpResponse resp;

        if (!networkReady(5000)) {
            resp.error = "WiFi not connected";
            return resp;
        }

        WiFiClientSecure client;
        client.setCACert(pem.c_str());

        HTTPClient http;
        if (!http.begin(client, url.c_str())) {
            resp.error = "http.begin failed";
            return resp;
        }

        http.setTimeout(15000);

        const int code = http.GET();
        resp.status_code = code;

        if (code > 0) {
            resp.body = http.getString().c_str();
        } else {
            resp.error = http.errorToString(code).c_str();
        }

        http.end();
        return resp;
    }
};

Platform& platform(const WifiConfig& wifiConfig) {
    static ArduinoPlatform instance(wifiConfig);
    return instance;
}

} // namespace forecast

#endif
