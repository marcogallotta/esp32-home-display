#include "ble.h"
#include "protocol.h"

#include "../platform.h"

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
        : config_(config) {
    }

    SwitchbotConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;
    UpdateCallback callback_;

    void upsertReading(
        const std::string& addr,
        const std::vector<std::uint8_t>& payload
    ) {
        if (!isMeterPayload(payload)) {
            return;
        }

        auto reading = decodeMeter(addr, payload, config_);
        if (!reading.has_value()) {
            return;
        }

        const std::int64_t lastSeenEpochS =
            platform::hasValidTime() ? static_cast<std::int64_t>(std::time(nullptr)) : 0;

        SensorReading out{
            reading->name,
            reading->shortName,
            reading->temperature_c,
            reading->humidity,
            lastSeenEpochS,
        };

        UpdateCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex);
            sensors[addr] = std::move(out);
            cb = callback_;
        }

        if (cb) {
            cb();
        }
    }

    // Note this is a callback that runs async, outside the main thread.
    void handleAdvertisement(const ble::AdvertisementEvent& event) {
        const auto it = event.manufacturerData.find(kSwitchbotManufacturerId);
        if (it == event.manufacturerData.end()) {
            return;
        }

        upsertReading(event.address, it->second);
    }

    void setUpdateCallback(UpdateCallback callback) {
        std::lock_guard<std::mutex> lock(mutex);
        callback_ = std::move(callback);
    }

    SensorMap snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return sensors;
    }
};

Scanner::Scanner(const SwitchbotConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

Scanner::~Scanner() = default;

void Scanner::start() {}
void Scanner::stop() {}
void Scanner::poll() {}

void Scanner::handleAdvertisement(const ble::AdvertisementEvent& event) {
    impl_->handleAdvertisement(event);
}

void Scanner::setUpdateCallback(UpdateCallback callback) {
    impl_->setUpdateCallback(std::move(callback));
}

SensorMap Scanner::snapshot() const {
    return impl_->snapshot();
}

} // namespace switchbot
