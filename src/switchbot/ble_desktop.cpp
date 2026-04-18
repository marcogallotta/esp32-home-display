#ifndef ARDUINO

#include "../config.h"
#include "ble.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

namespace switchbot {
namespace {

constexpr const char* kBluezService = "org.bluez";
constexpr const char* kObjectManagerPath = "/";
constexpr const char* kAdapterPath = "/org/bluez/hci0";
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kDeviceInterface = "org.bluez.Device1";
constexpr std::uint16_t kSwitchbotManufacturerId = 2409;

using VariantMap = std::map<std::string, sdbus::Variant>;
using InterfaceMap = std::map<std::string, VariantMap>;
using ManagedObjects = std::map<sdbus::ObjectPath, InterfaceMap>;

std::optional<std::string> getStringProperty(const VariantMap& props, const char* key) {
    auto it = props.find(key);
    if (it == props.end()) {
        return std::nullopt;
    }

    try {
        return it->second.get<std::string>();
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::int16_t> getInt16Property(const VariantMap& props, const char* key) {
    auto it = props.find(key);
    if (it == props.end()) {
        return std::nullopt;
    }

    try {
        return it->second.get<std::int16_t>();
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::map<std::uint16_t, std::vector<std::uint8_t>>> getManufacturerData(
    const VariantMap& props
) {
    auto it = props.find("ManufacturerData");
    if (it == props.end()) {
        return std::nullopt;
    }

    try {
        auto raw = it->second.get<std::map<std::uint16_t, sdbus::Variant>>();
        std::map<std::uint16_t, std::vector<std::uint8_t>> out;
        for (const auto& [id, value] : raw) {
            out[id] = value.get<std::vector<std::uint8_t>>();
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

struct Scanner::Impl {
    explicit Impl(const SwitchbotConfig& config)
        : config_(config) {
    }

    SwitchbotConfig config_;
    mutable std::mutex mutex;
    SensorMap sensors;
    std::map<std::string, std::string> pathToAddr;

    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IProxy> adapterProxy;
    std::unique_ptr<sdbus::IProxy> objectManagerProxy;
    std::optional<sdbus::Slot> propertiesChangedSlot;
    std::optional<sdbus::Slot> interfacesAddedSlot;

    bool running{false};

    void upsertReading(const std::string& addr,
                       int rssi,
                       const std::vector<std::uint8_t>& payload) {
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

    void handleDeviceProps(const std::string& addr, const VariantMap& props) {
        const auto manufacturerData = getManufacturerData(props);
        if (!manufacturerData.has_value()) {
            return;
        }

        auto mdIt = manufacturerData->find(kSwitchbotManufacturerId);
        if (mdIt == manufacturerData->end()) {
            return;
        }

        const int rssi = getInt16Property(props, "RSSI").value_or(0);
        upsertReading(addr, rssi, mdIt->second);
    }

    void primeFromExistingObjects() {
        ManagedObjects managedObjects;
        objectManagerProxy
            ->callMethod("GetManagedObjects")
            .onInterface(kObjectManagerInterface)
            .storeResultsTo(managedObjects);

        for (const auto& [path, interfaces] : managedObjects) {
            auto it = interfaces.find(kDeviceInterface);
            if (it == interfaces.end()) {
                continue;
            }

            auto addr = getStringProperty(it->second, "Address");
            if (!addr) {
                continue;
            }

            this->pathToAddr[static_cast<std::string>(path)] = *addr;
            handleDeviceProps(*addr, it->second);
        }
    }

    void installSignalHandler() {
        const std::string propsMatch =
            "type='signal',"
            "sender='org.bluez',"
            "interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged',";

        propertiesChangedSlot = connection->addMatch(
            propsMatch,
            [this](sdbus::Message msg) {
                std::string interfaceName;
                VariantMap changed;
                std::vector<std::string> invalidated;

                msg >> interfaceName >> changed >> invalidated;
                if (interfaceName != kDeviceInterface) {
                    return;
                }

                const std::string path = static_cast<std::string>(msg.getPath());
                if (path.rfind("/org/bluez/hci0/dev_", 0) != 0) {
                    return;
                }

                auto it = this->pathToAddr.find(path);
                if (it == this->pathToAddr.end()) {
                    return;
                }

                handleDeviceProps(it->second, changed);
            },
            sdbus::return_slot);

        const std::string addedMatch =
            "type='signal',"
            "sender='org.bluez',"
            "interface='org.freedesktop.DBus.ObjectManager',"
            "member='InterfacesAdded',";

        interfacesAddedSlot = connection->addMatch(
            addedMatch,
            [this](sdbus::Message msg) {
                sdbus::ObjectPath path;
                InterfaceMap interfaces;

                msg >> path >> interfaces;
                if (std::string(path).rfind("/org/bluez/hci0/dev_", 0) != 0) {
                    return;
                }

                auto it = interfaces.find(kDeviceInterface);
                if (it == interfaces.end()) {
                    return;
                }

                auto addr = getStringProperty(it->second, "Address");
                if (!addr) {
                    return;
                }

                this->pathToAddr[static_cast<std::string>(path)] = *addr;
                handleDeviceProps(*addr, it->second);
            },
            sdbus::return_slot);
    }

    void setDiscoveryFilter() {
        std::map<std::string, sdbus::Variant> filter;
        filter.emplace("Transport", sdbus::Variant{std::string{"le"}});
        filter.emplace("DuplicateData", sdbus::Variant{true});

        adapterProxy
            ->callMethod("SetDiscoveryFilter")
            .onInterface(kAdapterInterface)
            .withArguments(filter);
    }

    void startDiscovery() {
        adapterProxy
            ->callMethod("StartDiscovery")
            .onInterface(kAdapterInterface);
    }

    void stopDiscovery() {
        try {
            adapterProxy
                ->callMethod("StopDiscovery")
                .onInterface(kAdapterInterface);
        } catch (...) {
        }
    }

    void start() {
        if (running) {
            return;
        }

        connection = sdbus::createSystemBusConnection();
        adapterProxy = sdbus::createProxy(
            *connection,
            sdbus::ServiceName{kBluezService},
            sdbus::ObjectPath{kAdapterPath});
        objectManagerProxy = sdbus::createProxy(
            *connection,
            sdbus::ServiceName{kBluezService},
            sdbus::ObjectPath{kObjectManagerPath});

        installSignalHandler();
        setDiscoveryFilter();
        primeFromExistingObjects();
        startDiscovery();

        connection->enterEventLoopAsync();
        running = true;
    }

    void stop() {
        if (!running) {
            return;
        }

        stopDiscovery();
        propertiesChangedSlot.reset();
        interfacesAddedSlot.reset();

        if (connection) {
            connection->leaveEventLoop();
        }

        objectManagerProxy.reset();
        adapterProxy.reset();
        connection.reset();

        running = false;
    }

    void poll() {
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
