#ifndef ARDUINO

#include "scanner.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>

namespace ble {
namespace {

constexpr const char* kBluezService = "org.bluez";
constexpr const char* kObjectManagerPath = "/";
constexpr const char* kAdapterPath = "/org/bluez/hci0";
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kDeviceInterface = "org.bluez.Device1";

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

std::string normalizeUuidString(std::string uuid) {
    std::transform(
        uuid.begin(),
        uuid.end(),
        uuid.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (uuid.size() == 4) {
        return "0000" + uuid + "-0000-1000-8000-00805f9b34fb";
    }

    return uuid;
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

std::optional<std::map<std::string, std::vector<std::uint8_t>>> getServiceData(
    const VariantMap& props
) {
    auto it = props.find("ServiceData");
    if (it == props.end()) {
        return std::nullopt;
    }

    try {
        auto raw = it->second.get<std::map<std::string, sdbus::Variant>>();
        std::map<std::string, std::vector<std::uint8_t>> out;
        for (const auto& [uuid, value] : raw) {
            out[normalizeUuidString(uuid)] = value.get<std::vector<std::uint8_t>>();
        }
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

struct Scanner::Impl {
    explicit Impl(Callback callback)
        : callback_(std::move(callback)) {
    }

    Callback callback_;
    std::map<std::string, std::string> pathToAddr;

    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IProxy> adapterProxy;
    std::unique_ptr<sdbus::IProxy> objectManagerProxy;
    std::optional<sdbus::Slot> propertiesChangedSlot;
    std::optional<sdbus::Slot> interfacesAddedSlot;

    bool running{false};

    void emitEvent(const std::string& addr, const VariantMap& props) {
        if (!callback_) {
            return;
        }

        const auto manufacturerData = getManufacturerData(props);
        const auto serviceData = getServiceData(props);

        if ((!manufacturerData.has_value() || manufacturerData->empty()) &&
            (!serviceData.has_value() || serviceData->empty())) {
            return;
        }

        AdvertisementEvent event;
        event.address = addr;

        if (manufacturerData.has_value()) {
            event.manufacturerData = *manufacturerData;
        }
        if (serviceData.has_value()) {
            event.serviceData = *serviceData;
        }

        callback_(event);
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

            pathToAddr[static_cast<std::string>(path)] = *addr;
            emitEvent(*addr, it->second);
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

                auto it = pathToAddr.find(path);
                if (it == pathToAddr.end()) {
                    return;
                }

                emitEvent(it->second, changed);
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

                pathToAddr[static_cast<std::string>(path)] = *addr;
                emitEvent(*addr, it->second);
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
        pathToAddr.clear();

        running = false;
    }

    void poll() {
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
