#ifdef ARDUINO

#include "scanner.h"

#include <NimBLEDevice.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ble {

namespace {
constexpr uint32_t kScanTimeMs = 30 * 1000;
}

struct Scanner::Impl {
    explicit Impl(Callback callback)
        : callback_(std::move(callback)) {
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

            std::string addr = advertisedDevice->getAddress().toString();
            std::transform(
                addr.begin(),
                addr.end(),
                addr.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            AdvertisementEvent event;
            event.address = std::move(addr);
            event.rssi = advertisedDevice->getRSSI();
            event.manufacturerData[manufacturerId] = std::vector<std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(manufacturerData.data() + 2),
                reinterpret_cast<const std::uint8_t*>(manufacturerData.data() + manufacturerData.size())
            );

            impl_.emitEvent(event);
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

    Callback callback_;
    NimBLEScan* scan{nullptr};
    std::atomic<bool> running{false};
    std::atomic<bool> restartRequested{false};
    bool nimbleInitialised{false};
    std::unique_ptr<ScanCallbacks> callbacks;

    void emitEvent(const AdvertisementEvent& event) {
        if (callback_) {
            callback_(event);
        }
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
};

Scanner::Scanner(Callback callback)
    : impl_(std::make_unique<Impl>(std::move(callback))) {
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

} // namespace ble

#endif
