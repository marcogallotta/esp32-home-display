#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ble {

struct AdvertisementEvent {
    std::string address;
    int rssi = 0;
    std::map<std::uint16_t, std::vector<std::uint8_t>> manufacturerData;
};

class Scanner {
public:
    using Callback = std::function<void(const AdvertisementEvent&)>;

    explicit Scanner(Callback callback);
    ~Scanner();

    void start();
    void stop();
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ble