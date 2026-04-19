#ifdef ARDUINO

#include "../ble/scanner.h"
#include "ble.h"
#include "protocol.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace switchbot {

namespace {
constexpr std::uint16_t kSwitchbotManufacturerId = 2409;
}

struct Scanner::Impl {
    explicit Impl(const SwitchbotConfig& config)
        : config_(config),
          scanner_([this](const ble::AdvertisementEvent& event) {
              handleAdvertisement(event);
          }) {
    }

    SwitchbotConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;
    ble::Scanner scanner_;

    void upsertReading(
        const std::string& addr,
        int rssi,
        const std::vector<std::uint8_t>& payload
    ) {
        if (!isMeterPayload(payload)) {
            return;
        }

        auto reading = decodeMeter(addr, payload, config_);
        if (!reading.has_value()) {
            return;
        }

        SensorReading out{
            reading->name,
            reading->shortName,
            reading->temperature_c,
            reading->humidity,
            static_cast<std::int64_t>(std::time(nullptr)),
            rssi,
        };

        std::lock_guard<std::mutex> lock(mutex);
        sensors[addr] = std::move(out);
    }

    void handleAdvertisement(const ble::AdvertisementEvent& event) {
        const auto it = event.manufacturerData.find(kSwitchbotManufacturerId);
        if (it == event.manufacturerData.end()) {
            return;
        }

        upsertReading(event.address, event.rssi, it->second);
    }

    void start() {
        scanner_.start();
    }

    void stop() {
        scanner_.stop();
    }

    void poll() {
        scanner_.poll();
    }

    SensorMap snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return sensors;
    }
};

Scanner::Scanner(const SwitchbotConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

Scanner::~Scanner() {
    stop();
}

void Scanner::start() {
    impl_->start();
}

void Scanner::stop() {
    impl_->stop();
}

void Scanner::poll() {
    impl_->poll();
}

SensorMap Scanner::snapshot() const {
    return impl_->snapshot();
}

} // namespace switchbot

#endif
