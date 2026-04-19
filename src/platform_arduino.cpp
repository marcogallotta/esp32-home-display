#ifdef ARDUINO

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "platform.h"

namespace {

bool connectWifi(const WifiConfig& wifiConfig, unsigned long timeoutMs = 15000) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= timeoutMs) {
            return false;
        }
        delay(500);
    }

    return true;
}

} // namespace

namespace platform {

bool initTime(const Config& config, unsigned long timeoutMs) {
    const uint32_t start = millis();

    if (!connectWifi(config.wifi, timeoutMs)) {
        return false;
    }

    configTime(0, 0, "pool.ntp.org");
    setenv("TZ", config.location.timezoneLong.c_str(), 1);
    tzset();

    struct tm timeinfo{};
    while (!getLocalTime(&timeinfo, 500)) { // Retry every 500ms until we get the time
        if (millis() - start >= timeoutMs) {
            return false;
        }
    }

    return true;
}

bool hasValidTime() {
    struct tm timeinfo{};
    return getLocalTime(&timeinfo, 0);
}

void delayMs(int ms) {
    delay(ms);
}

void printLine(const std::string& s) {
    Serial.println(s.c_str());
}

} // namespace platform

#endif
