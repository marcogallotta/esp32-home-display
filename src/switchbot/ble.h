#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "../ble/scanner.h"
#include "protocol.h"

namespace switchbot {

struct SensorReading {
    std::string name;
    std::string shortName;
    float temperature_c;
    std::uint8_t humidity;
    std::int64_t last_seen_epoch_s;
    int rssi;
};

using SensorMap = std::map<std::string, SensorReading>;

class Scanner {
public:
    using UpdateCallback = std::function<void()>;

    explicit Scanner(const SwitchbotConfig& config);
    ~Scanner();

    void start();
    void stop();
    void poll();

    void handleAdvertisement(const ble::AdvertisementEvent& event);
    void setUpdateCallback(UpdateCallback callback);

    SensorMap snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace switchbot
