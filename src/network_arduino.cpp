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

TransportResult mapHttpClientError(int code) {
    switch (code) {
        case HTTPC_ERROR_CONNECTION_REFUSED:
        case HTTPC_ERROR_CONNECTION_LOST:
        case HTTPC_ERROR_SEND_HEADER_FAILED:
        case HTTPC_ERROR_SEND_PAYLOAD_FAILED:
        case HTTPC_ERROR_NOT_CONNECTED:
            return TransportResult::NetworkError;

        case HTTPC_ERROR_READ_TIMEOUT:
            return TransportResult::Timeout;

        case HTTPC_ERROR_ENCODING:
        case HTTPC_ERROR_TOO_LESS_RAM:
            return TransportResult::InternalError;

        default:
            return TransportResult::NetworkError;
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
            resp.transport = TransportResult::NetworkError;
            return resp;
        }

        WiFiClientSecure client;
        configureTlsClient(client, request.pem, *this, request.url);

        HTTPClient http;
        if (!http.begin(client, request.url.c_str())) {
            resp.transport = TransportResult::InternalError;
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

        if (code > 0) {
            resp.transport = TransportResult::Ok;
            resp.statusCode = code;
            resp.body = http.getString().c_str();
        } else {
            resp.transport = mapHttpClientError(code);
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
