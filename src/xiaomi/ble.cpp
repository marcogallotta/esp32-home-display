#include "ble.h"
#include "protocol.h"

#include "../platform.h"

#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace xiaomi {

struct Scanner::Impl {
    explicit Impl(const XiaomiConfig& config)
        : config_(config) {
    }

    XiaomiConfig config_;
    SensorMap sensors;

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

    bool handleAdvertisement(const ble::AdvertisementEvent& event) {
        const XiaomiSensorConfig* sensor = findSensorConfig(config_, event.address);
        if (sensor == nullptr) {
            return false;
        }

        bool changed = false;

        for (const auto& [uuid, payload] : event.serviceData) {
            if (!isXiaomiServiceDataUuid(uuid)) {
                continue;
            }

            const auto decoded = decodeObject(payload);
            if (!decoded.has_value()) {
                continue;
            }

            SensorReading& reading = sensors[event.address];
            reading.name = sensor->name;
            reading.shortName = sensor->shortName;
            reading.lastSeenEpochS =
                platform::hasValidTime() ? static_cast<std::int64_t>(std::time(nullptr)) : 0;

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

        return changed;
    }

    SensorMap snapshot() const {
        return sensors;
    }
};

Scanner::Scanner(const XiaomiConfig& config)
    : impl_(std::make_unique<Impl>(config)) {
}

Scanner::~Scanner() = default;

bool Scanner::handleAdvertisement(const ble::AdvertisementEvent& event) {
    return impl_->handleAdvertisement(event);
}

SensorMap Scanner::snapshot() const {
    return impl_->snapshot();
}

} // namespace xiaomi
