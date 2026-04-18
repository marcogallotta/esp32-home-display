#ifdef ARDUINO

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "platform.h"

namespace {

void connectWifi(const WifiConfig& wifiConfig) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiConfig.ssid.c_str(), wifiConfig.password.c_str());

    while (WiFi.status() != WL_CONNECTED) {
        // TODO: debug output and retry
        delay(500);
    }
}

} // namespace

namespace platform {

void initTime(const Config& config) {
    connectWifi(config.wifi);

    configTime(0, 0, "pool.ntp.org");
    setenv("TZ", config.location.timezoneLong.c_str(), 1);
    tzset();

    time_t now = time(nullptr);
    while (now < 100000) { // TODO: make non-blocking
        delay(500);
        now = time(nullptr);
    }
}

void delayMs(int ms) {
    delay(ms);
}

void printLine(const std::string& s) {
    Serial.println(s.c_str());
}

} // namespace platform

#endif