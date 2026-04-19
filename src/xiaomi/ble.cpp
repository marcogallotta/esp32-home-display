#include "ble.h"
#include "protocol.h"

#include "../platform.h"

#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xiaomi {

struct Scanner::Impl {
    explicit Impl(const XiaomiConfig& config)
        : config_(config) {
    }

    XiaomiConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;
    UpdateCallback callback_;

    void applyObject(SensorReading& reading, const DecodedObject& decoded) {
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

    // Note this is a callback that runs async, outside the main thread.
    void handleAdvertisement(const ble::AdvertisementEvent& event) {
        const XiaomiSensorConfig* sensor = findSensorConfig(config_, event.address);
        if (sensor == nullptr) {
            return;
        }

        bool matched = false;
        bool changed = false;
        UpdateCallback cb;

        {
            std::lock_guard<std::mutex> lock(mutex);
            SensorReading& reading = sensors[event.address];
            reading.name = sensor->name;
            reading.shortName = sensor->shortName;
            reading.rssi = event.rssi;
            reading.lastSeenEpochS =
                platform::hasValidTime() ? static_cast<std::int64_t>(std::time(nullptr)) : 0;

            for (const auto& [uuid, payload] : event.serviceData) {
                if (!isXiaomiServiceDataUuid(uuid)) {
                    continue;
                }

                const auto decoded = decodeObject(payload);
                if (!decoded.has_value()) {
                    continue;
                }

                matched = true;

                const SensorReading before = reading;
                applyObject(reading, *decoded);

                if (reading.hasTemperature != before.hasTemperature ||
                    reading.temperatureC != before.temperatureC ||
                    reading.hasLux != before.hasLux ||
                    reading.lux != before.lux ||
                    reading.hasMoisture != before.hasMoisture ||
                    reading.moisturePct != before.moisturePct ||
                    reading.hasConductivity != before.hasConductivity ||
                    reading.conductivityUsCm != before.conductivityUsCm) {
                    changed = true;
                }
            }

            if (!matched &&
                !reading.hasTemperature &&
                !reading.hasLux &&
                !reading.hasMoisture &&
                !reading.hasConductivity) {
                sensors.erase(event.address);
            }

            if (changed) {
                cb = callback_;
            }
        }

        if (cb) {
            cb();
        }
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

Scanner::Scanner(const XiaomiConfig& config)
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

} // namespace xiaomi
