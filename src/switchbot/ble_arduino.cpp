#ifdef ARDUINO

#include "switchbot/ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace switchbot {

namespace {
constexpr std::uint16_t kSwitchbotManufacturerId = 2409;
constexpr uint32_t kScanTimeMs = 30 * 1000;
}

struct Scanner::Impl {
    explicit Impl(const SwitchbotConfig& config)
        : config_(config) {
    }

    class ScanCallbacks : public NimBLEScanCallbacks {
    public:
        explicit ScanCallbacks(Impl& impl)
            : impl_(impl) {
        }

        void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
            if (advertisedDevice == nullptr) {
                return;
            }

            if (!advertisedDevice->haveManufacturerData()) {
                return;
            }

            const std::string manufacturerData = advertisedDevice->getManufacturerData();
            if (manufacturerData.size() < 2) {
                return;
            }

            const std::uint8_t b0 = static_cast<std::uint8_t>(manufacturerData[0]);
            const std::uint8_t b1 = static_cast<std::uint8_t>(manufacturerData[1]);
            const std::uint16_t manufacturerId = static_cast<std::uint16_t>(b0 | (b1 << 8));
            if (manufacturerId != kSwitchbotManufacturerId) {
                return;
            }

            std::string addr = advertisedDevice->getAddress().toString();
            std::transform(
                addr.begin(),
                addr.end(),
                addr.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            impl_.upsertReading(
                addr,
                advertisedDevice->getRSSI(),
                reinterpret_cast<const std::uint8_t*>(manufacturerData.data() + 2),
                manufacturerData.size() - 2);
        }

        void onScanEnd(const NimBLEScanResults& results, int reason) override {
            (void)results;
            (void)reason;

            if (impl_.running.load()) {
                impl_.restartRequested.store(true);
            }
        }

    private:
        Impl& impl_;
    };

    SwitchbotConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;

    NimBLEScan* scan{nullptr};
    std::atomic<bool> running{false};
    std::atomic<bool> restartRequested{false};
    bool nimbleInitialised{false};
    std::unique_ptr<ScanCallbacks> callbacks;

    void upsertReading(
        const std::string& addr,
        int rssi,
        const std::uint8_t* payloadData,
        std::size_t payloadSize
    ) {
        if (payloadData == nullptr || payloadSize == 0) {
            return;
        }

        std::vector<std::uint8_t> payload(payloadData, payloadData + payloadSize);
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

    void start() {
        if (running.load()) {
            return;
        }

        if (!nimbleInitialised) {
            NimBLEDevice::init("");
            nimbleInitialised = true;
        }

        scan = NimBLEDevice::getScan();
        if (!callbacks) {
            callbacks = std::make_unique<ScanCallbacks>(*this);
        }

        restartRequested.store(false);
        running.store(true);

        scan->setScanCallbacks(callbacks.get(), false);
        scan->setActiveScan(false);
        scan->setMaxResults(0);
        scan->start(kScanTimeMs, false, true);
    }

    void stop() {
        if (!running.load()) {
            return;
        }

        running.store(false);
        restartRequested.store(false);

        if (scan != nullptr) {
            scan->stop();
        }
    }

    void poll() {
        if (!running.load()) {
            return;
        }

        if (!restartRequested.load()) {
            return;
        }

        if (scan == nullptr) {
            return;
        }

        if (scan->isScanning()) {
            return;
        }

        restartRequested.store(false);
        scan->start(kScanTimeMs, false, true);
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
