#include "esp32_arduino_transport.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <FS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <string>

namespace pqueue::http {
namespace {

std::string readFileToString(fs::FS* fileSystem, const char* path) {
    if (fileSystem == nullptr || path == nullptr || path[0] == '\0') {
        return {};
    }

    File file = fileSystem->open(path, "r");
    if (!file) {
        return {};
    }

    std::string contents;
    contents.reserve(static_cast<std::size_t>(file.size()));
    while (file.available()) {
        contents.push_back(static_cast<char>(file.read()));
    }
    file.close();
    return contents;
}

} // namespace

Esp32ArduinoTransport::Esp32ArduinoTransport(Esp32ArduinoTransportConfig config)
    : config_(config) {}

Response Esp32ArduinoTransport::post(
    const char* url,
    const Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
) {
    if (url == nullptr) {
        return {kNoStatusCode, TransportError::Unknown};
    }

    if (!isNetworkReady()) {
        return {kNoStatusCode, TransportError::Network};
    }

    WiFiClientSecure client;
    if (!configureTlsClient(client)) {
        return {kNoStatusCode, TransportError::Tls};
    }

    HTTPClient http;
    if (!http.begin(client, url)) {
        return {kNoStatusCode, TransportError::Unknown};
    }

    http.setTimeout(static_cast<std::uint16_t>(config_.common.timeoutMs));
    if (config_.common.userAgent != nullptr) {
        http.setUserAgent(config_.common.userAgent);
    }
    http.setFollowRedirects(config_.common.followRedirects ? HTTPC_STRICT_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);

    for (std::size_t i = 0; i < headerCount; ++i) {
        if (headers[i].name == nullptr || headers[i].value == nullptr) {
            continue;
        }
        http.addHeader(headers[i].name, headers[i].value);
    }

    int code = http.POST(const_cast<std::uint8_t*>(body), bodySize);
    Response response;
    if (code > 0) {
        response.statusCode = code;
        response.error = TransportError::None;
        response.body = http.getString().c_str();
    } else {
        response.statusCode = kNoStatusCode;
        response.error = mapHttpClientError(code);
    }

    http.end();
    return response;
}

bool Esp32ArduinoTransport::isNetworkReady() const {
    if (config_.networkReady != nullptr) {
        return config_.networkReady(config_.networkReadyContext, config_.common.timeoutMs);
    }
    return WiFi.status() == WL_CONNECTED;
}

TransportError Esp32ArduinoTransport::mapHttpClientError(int code) const {
    switch (code) {
        case HTTPC_ERROR_READ_TIMEOUT:
            return TransportError::Timeout;
        case HTTPC_ERROR_CONNECTION_REFUSED:
        case HTTPC_ERROR_CONNECTION_LOST:
        case HTTPC_ERROR_NOT_CONNECTED:
            return TransportError::Network;
        default:
            return TransportError::Unknown;
    }
}

bool Esp32ArduinoTransport::configureTlsClient(WiFiClientSecure& client) const {
    if (config_.common.allowInsecureTls) {
        client.setInsecure();
        return true;
    }

    const std::string caCert = readFileToString(config_.caCertFileSystem, config_.caCertPath);
    if (caCert.empty()) {
        return false;
    }

    client.setCACert(caCert.c_str());
    return true;
}

} // namespace pqueue::http

#endif // ARDUINO
