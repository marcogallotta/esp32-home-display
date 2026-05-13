#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ble {

struct AdvertisementEvent {
    std::string address;
    std::map<std::uint16_t, std::vector<std::uint8_t>> manufacturerData;
    std::map<std::string, std::vector<std::uint8_t>> serviceData;
};

class EventQueue;

class Scanner {
public:
    explicit Scanner(EventQueue& queue);
    ~Scanner();

    void start();
    void stop();
    void poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ble
