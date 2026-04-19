#ifdef ARDUINO

#include "../ble/scanner.h"
#include "ble.h"
#include "protocol.h"

#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xiaomi {

struct Scanner::Impl {
    explicit Impl(const XiaomiConfig& config)
        : config_(config),
          scanner_([this](const ble::AdvertisementEvent& event) {
              handleAdvertisement(event);
          }) {
    }

    XiaomiConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;
    ble::Scanner scanner_;

    void applyObject(
        SensorReading& reading,
        const DecodedObject& decoded
    ) {
        switch (decoded.kind) {
            case DecodedObject::Kind::Temperature:
                reading.hasTemperature = true;
                reading.temperatureC = decoded.temperatureC;
                break;

            case DecodedObject::Kind::Lux:
                reading.hasLux = true;
                reading.lux = decoded.lux;
                break;

            case DecodedObject::Kind::Moisture:
                reading.hasMoisture = true;
                reading.moisturePct = decoded.moisturePct;
                break;

            case DecodedObject::Kind::Conductivity:
                reading.hasConductivity = true;
                reading.conductivityUsCm = decoded.conductivityUsCm;
                break;
        }
    }

    void handleAdvertisement(const ble::AdvertisementEvent& event) {
        const XiaomiSensorConfig* sensor = findSensorConfig(config_, event.address);
        if (sensor == nullptr) {
            return;
        }

        bool matched = false;

        std::lock_guard<std::mutex> lock(mutex);
        SensorReading& reading = sensors[event.address];
        reading.name = sensor->name;
        reading.shortName = sensor->shortName;
        reading.rssi = event.rssi;
        reading.lastSeenEpochS = static_cast<std::int64_t>(std::time(nullptr));

        for (const auto& [uuid, payload] : event.serviceData) {
            if (!isXiaomiServiceDataUuid(uuid)) {
                continue;
            }

            const auto decoded = decodeObject(payload);
            if (!decoded.has_value()) {
                continue;
            }

            applyObject(reading, *decoded);
            matched = true;
        }

        if (!matched && !reading.hasTemperature && !reading.hasLux &&
            !reading.hasMoisture && !reading.hasConductivity) {
            sensors.erase(event.address);
        }
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

Scanner::Scanner(const XiaomiConfig& config)
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

} // namespace xiaomi

#endif
