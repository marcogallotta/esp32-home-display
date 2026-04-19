#ifdef ARDUINO

#include "scanner.h"

#include <NimBLEDevice.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ble {

namespace {
constexpr uint32_t kScanTimeMs = 30 * 1000;

std::string normalizeUuidString(std::string uuid) {
    std::transform(
        uuid.begin(),
        uuid.end(),
        uuid.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (uuid.size() == 4) {
        char buf[37];
        std::snprintf(
            buf,
            sizeof(buf),
            "0000%s-0000-1000-8000-00805f9b34fb",
            uuid.c_str());
        return std::string(buf);
    }

    return uuid;
}
} // namespace

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

            AdvertisementEvent event;

            std::string addr = advertisedDevice->getAddress().toString();
            std::transform(
                addr.begin(),
                addr.end(),
                addr.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            event.address = std::move(addr);
            event.rssi = advertisedDevice->getRSSI();

            if (advertisedDevice->haveManufacturerData()) {
                const std::string manufacturerData = advertisedDevice->getManufacturerData();
                if (manufacturerData.size() >= 2) {
                    const std::uint8_t b0 = static_cast<std::uint8_t>(manufacturerData[0]);
                    const std::uint8_t b1 = static_cast<std::uint8_t>(manufacturerData[1]);
                    const std::uint16_t manufacturerId =
                        static_cast<std::uint16_t>(b0 | (b1 << 8));

                    event.manufacturerData[manufacturerId] = std::vector<std::uint8_t>(
                        reinterpret_cast<const std::uint8_t*>(manufacturerData.data() + 2),
                        reinterpret_cast<const std::uint8_t*>(manufacturerData.data() + manufacturerData.size())
                    );
                }
            }

            for (int i = 0; i < advertisedDevice->getServiceDataCount(); ++i) {
                const NimBLEUUID serviceUuid = advertisedDevice->getServiceDataUUID(i);
                const std::string payload = advertisedDevice->getServiceData(i);

                event.serviceData[normalizeUuidString(serviceUuid.toString())] =
                    std::vector<std::uint8_t>(
                        reinterpret_cast<const std::uint8_t*>(payload.data()),
                        reinterpret_cast<const std::uint8_t*>(payload.data() + payload.size())
                    );
            }

            if (event.manufacturerData.empty() && event.serviceData.empty()) {
                return;
            }

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
