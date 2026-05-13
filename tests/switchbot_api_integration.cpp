#include "api/outbox_client.h"
#include "api/state.h"
#include "api_sync.h"
#include "config.h"
#include "state.h"
#include "switchbot/ble.h"
#include "update.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"
#include "pqueue/http/outbox.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kSpoolDir = "build/pqueue-spools/src_switchbot_integration";

constexpr std::uint16_t kManufacturerId = 2409;
constexpr const char* kMac = "AA:BB:CC:DD:EE:FF";
constexpr std::int64_t kNow = 1'710'000'000;

struct FakeClock {
    std::uint64_t nowMs = 1000;
};

std::uint64_t fakeClockNow(void* ctx) {
    return static_cast<FakeClock*>(ctx)->nowMs;
}

struct PostedRequest {
    std::string url;
    std::string body;
};

struct FakeTransport final : pqueue::http::Transport {
    std::vector<PostedRequest> posts;

    pqueue::http::Response post(
        const char* url,
        const pqueue::http::Header*,
        std::size_t,
        const std::uint8_t* body,
        std::size_t bodySize
    ) override {
        posts.push_back({
            url ? std::string{url} : std::string{},
            std::string(reinterpret_cast<const char*>(body), bodySize),
        });
        return {200, pqueue::http::TransportError::None, "{\"status\":\"ok\",\"result\":\"created\"}"};
    }
};

Config makeConfig() {
    Config config;
    config.api.baseUrl = "https://example.test/api";
    config.api.apiKey = "test-key";
    config.api.outbox.diskReserveBytes = 64 * 1024;
    config.api.outbox.drainRateCap = 10;
    config.api.outbox.retryDelayMs = 1000;
    config.api.outbox.logLevel = api::PqueueLogLevel::None;

    SwitchbotSensorConfig sensor;
    sensor.mac = kMac;
    sensor.name = "Bedroom";
    sensor.shortName = "Bed";
    config.switchbot.sensors.push_back(sensor);

    return config;
}

ble::AdvertisementEvent makeAdvertisement(
    std::uint8_t tempDecimal = 0x04,
    std::uint8_t tempIntRaw = 0x97,
    std::uint8_t humidityRaw = 0x48
) {
    ble::AdvertisementEvent event;
    event.address = kMac;
    event.manufacturerData[kManufacturerId] = {
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
        0x00, 0x00,
        tempDecimal,
        tempIntRaw,
        humidityRaw,
        0x01,
    };
    return event;
}

std::string prepareSpoolDir() {
    std::error_code ec;
    std::filesystem::remove_all(kSpoolDir, ec);
    std::filesystem::create_directories(kSpoolDir);
    return kSpoolDir.string();
}

struct Fixture {
    Config config;
    FakeTransport transport;
    FakeClock clock;
    api::OutboxClient client;
    switchbot::Scanner scanner;
    State state;
    api::State apiState;

    Fixture()
        : config(makeConfig()),
          client(config, transport, prepareSpoolDir(), fakeClockNow, &clock),
          scanner(config.switchbot) {
        state.switchbotSensors.resize(config.switchbot.sensors.size());
        api::initState(state, apiState);
    }
};

} // namespace

TEST_CASE("switchbot integration: valid advert reaches API POST") {
    Fixture f;

    f.scanner.handleAdvertisement(makeAdvertisement());
    updateSwitchbotState(f.config, kNow, f.scanner, f.state);

    REQUIRE(f.state.switchbotSensors[0].reading.hasCompleteReading());
    CHECK(f.state.switchbotSensors[0].reading.temperatureC == doctest::Approx(23.4f));
    CHECK_EQ(*f.state.switchbotSensors[0].reading.humidityPct, 72);

    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    REQUIRE_EQ(f.transport.posts.size(), 1U);
    CHECK_EQ(f.transport.posts[0].url, "https://example.test/api/switchbot/reading");

    StaticJsonDocument<512> doc;
    REQUIRE_FALSE(deserializeJson(doc, f.transport.posts[0].body));
    CHECK_EQ(doc["mac"].as<std::string>(), kMac);
    CHECK_EQ(doc["name"].as<std::string>(), "Bedroom");
    CHECK_EQ(doc["type"].as<std::string>(), "switchbot");
    CHECK_FALSE(doc["timestamp"].isNull());
    CHECK(doc["temperature_c"].as<float>() == doctest::Approx(23.4f));
    CHECK_EQ(doc["humidity_pct"].as<int>(), 72);
}

TEST_CASE("switchbot integration: second identical sync does not resend") {
    Fixture f;

    f.scanner.handleAdvertisement(makeAdvertisement());
    updateSwitchbotState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    REQUIRE_EQ(f.transport.posts.size(), 1U);

    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    CHECK_EQ(f.transport.posts.size(), 1U);
}

TEST_CASE("switchbot integration: no advert produces no POST") {
    Fixture f;

    updateSwitchbotState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    CHECK(f.transport.posts.empty());
}

TEST_CASE("switchbot integration: updated reading is resynced") {
    Fixture f;

    f.scanner.handleAdvertisement(makeAdvertisement(0x04, 0x97, 0x48));
    updateSwitchbotState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);
    REQUIRE_EQ(f.transport.posts.size(), 1U);

    f.scanner.handleAdvertisement(makeAdvertisement(0x06, 0x05, 0x42));
    updateSwitchbotState(f.config, kNow + 60, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow + 60);

    REQUIRE_EQ(f.transport.posts.size(), 2U);

    StaticJsonDocument<512> doc;
    REQUIRE_FALSE(deserializeJson(doc, f.transport.posts[1].body));
    CHECK(doc["temperature_c"].as<float>() == doctest::Approx(-5.6f));
    CHECK_EQ(doc["humidity_pct"].as<int>(), 66);
}
