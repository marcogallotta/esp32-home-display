#include "api/outbox_client.h"
#include "api/state.h"
#include "api_sync.h"
#include "config.h"
#include "state.h"
#include "update.h"
#include "xiaomi/ble.h"
#include "xiaomi_helpers.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"
#include "pqueue/http/outbox.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kSpoolDir = "build/pqueue-spools/xiaomi_integration";

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

    XiaomiSensorConfig sensor;
    sensor.mac = kMac;
    sensor.name = "Basil";
    sensor.shortName = "Bas";
    config.xiaomi.sensors.push_back(sensor);

    return config;
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
    xiaomi::Scanner scanner;
    State state;
    api::State apiState;

    Fixture()
        : config(makeConfig()),
          client(config, transport, prepareSpoolDir(), fakeClockNow, &clock),
          scanner(config.xiaomi) {
        state.xiaomiSensors.resize(config.xiaomi.sensors.size());
        api::initState(state, apiState);
    }

    void feedAdvert(std::uint16_t objectId, const std::vector<std::uint8_t>& data) {
        scanner.handleAdvertisement(xiaomi_test::xiaomiAdvertisement(
            kMac, xiaomi_test::kFe95Uuid, xiaomi_test::xiaomiPayload(objectId, data)
        ));
    }
};

} // namespace

TEST_CASE("xiaomi integration: updateXiaomiState converts snapshot into State") {
    Fixture f;

    f.feedAdvert(xiaomi_test::kTempObjectId,         {0xEA, 0x00});       // 23.4°C
    f.feedAdvert(xiaomi_test::kMoistureObjectId,     {42});               // 42%
    f.feedAdvert(xiaomi_test::kLuxObjectId,          {0x56, 0x34, 0x12}); // 0x123456
    f.feedAdvert(xiaomi_test::kConductivityObjectId, {0xF4, 0x01});       // 500 µS/cm

    updateXiaomiState(f.config, kNow, f.scanner, f.state);

    const auto& reading = f.state.xiaomiSensors[0].reading;
    REQUIRE(reading.temperatureC.has_value());
    CHECK(reading.temperatureC == doctest::Approx(23.4f));
    REQUIRE(reading.moisturePct.has_value());
    CHECK_EQ(*reading.moisturePct, 42);
    REQUIRE(reading.lux.has_value());
    CHECK_EQ(*reading.lux, 0x123456);
    REQUIRE(reading.conductivityUsCm.has_value());
    CHECK_EQ(*reading.conductivityUsCm, 500);
    CHECK(reading.lastSeenEpochS.has_value());
}

TEST_CASE("xiaomi integration: updateXiaomiState resets reading when sensor absent from snapshot") {
    Fixture f;

    f.state.xiaomiSensors[0].reading.temperatureC = 20.0f;

    updateXiaomiState(f.config, kNow, f.scanner, f.state);

    CHECK_FALSE(f.state.xiaomiSensors[0].reading.hasAnyValue());
}

TEST_CASE("xiaomi integration: valid full adverts reach API POST") {
    Fixture f;

    f.feedAdvert(xiaomi_test::kTempObjectId,         {0xEA, 0x00});
    f.feedAdvert(xiaomi_test::kMoistureObjectId,     {42});
    f.feedAdvert(xiaomi_test::kLuxObjectId,          {0x56, 0x34, 0x12});
    f.feedAdvert(xiaomi_test::kConductivityObjectId, {0xF4, 0x01});

    updateXiaomiState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    REQUIRE_EQ(f.transport.posts.size(), 1U);
    CHECK_EQ(f.transport.posts[0].url, "https://example.test/api/xiaomi/reading");

    StaticJsonDocument<512> doc;
    REQUIRE_FALSE(deserializeJson(doc, f.transport.posts[0].body));
    CHECK_EQ(doc["mac"].as<std::string>(), kMac);
    CHECK_EQ(doc["name"].as<std::string>(), "Basil");
    CHECK_EQ(doc["type"].as<std::string>(), "xiaomi");
    CHECK_FALSE(doc["timestamp"].as<std::string>().empty());
    CHECK(doc["temperature_c"].as<float>() == doctest::Approx(23.4f));
    CHECK_EQ(doc["moisture_pct"].as<int>(), 42);
    CHECK_EQ(doc["light_lux"].as<int>(), 0x123456);
    CHECK_EQ(doc["conductivity_us_cm"].as<int>(), 500);
}

TEST_CASE("xiaomi integration: second identical sync does not resend") {
    Fixture f;

    f.feedAdvert(xiaomi_test::kTempObjectId,         {0xEA, 0x00});
    f.feedAdvert(xiaomi_test::kMoistureObjectId,     {42});
    f.feedAdvert(xiaomi_test::kLuxObjectId,          {0x56, 0x34, 0x12});
    f.feedAdvert(xiaomi_test::kConductivityObjectId, {0xF4, 0x01});

    updateXiaomiState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);
    REQUIRE_EQ(f.transport.posts.size(), 1U);

    syncApiState(f.config, f.state, f.apiState, f.client, kNow);
    CHECK_EQ(f.transport.posts.size(), 1U);
}

TEST_CASE("xiaomi integration: partial reading opens pending and later flushes") {
    Fixture f;

    f.feedAdvert(xiaomi_test::kTempObjectId, {0xEA, 0x00});

    updateXiaomiState(f.config, kNow, f.scanner, f.state);
    syncApiState(f.config, f.state, f.apiState, f.client, kNow);

    CHECK(f.transport.posts.empty());
    REQUIRE(f.apiState.xiaomi.pending[0].active);

    const auto opened = f.apiState.xiaomi.pending[0].openedAtEpochS;

    syncApiState(f.config, f.state, f.apiState, f.client, opened + 60);

    REQUIRE_EQ(f.transport.posts.size(), 1U);
    CHECK_EQ(f.transport.posts[0].url, "https://example.test/api/xiaomi/reading");

    StaticJsonDocument<512> doc;
    REQUIRE_FALSE(deserializeJson(doc, f.transport.posts[0].body));
    CHECK(doc["temperature_c"].as<float>() == doctest::Approx(23.4f));
    CHECK_FALSE(doc["timestamp"].as<std::string>().empty());
    CHECK(doc["moisture_pct"].isNull());
    CHECK(doc["light_lux"].isNull());
    CHECK(doc["conductivity_us_cm"].isNull());
}
