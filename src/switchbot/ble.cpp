#include "ble.h"
#include "protocol.h"

#include "../platform.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
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
    SensorMap sensors;

    bool upsertReading(
        const std::string& addr,
        const std::vector<std::uint8_t>& payload
    ) {
        if (!isMeterPayload(payload)) {
            return false;
        }

        auto reading = decodeMeter(addr, payload, config_);
        if (!reading.has_value()) {
            return false;
        }

        const std::int64_t lastSeenEpochS =
            platform::hasValidTime() ? static_cast<std::int64_t>(std::time(nullptr)) : 0;

        sensors.insert_or_assign(addr, SensorReading{
            reading->name,
            reading->shortName,
            reading->temperature_c,
            reading->humidity,
            lastSeenEpochS,
        });

        return true;
    }

    bool handleAdvertisement(const ble::AdvertisementEvent& event) {
        const auto it = event.manufacturerData.find(kSwitchbotManufacturerId);
        if (it == event.manufacturerData.end()) {
            return false;
        }

        return upsertReading(event.address, it->second);
    }

    SensorMap snapshot() const {
        return sensors;
    }
};

Scanner::Scanner(const SwitchbotConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

Scanner::~Scanner() = default;

bool Scanner::handleAdvertisement(const ble::AdvertisementEvent& event) {
    return impl_->handleAdvertisement(event);
}

SensorMap Scanner::snapshot() const {
    return impl_->snapshot();
}

} // namespace switchbot
