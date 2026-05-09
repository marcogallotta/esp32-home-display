#include "api/outbox_client.h"

#include "doctest/doctest.h"
#include "ArduinoJson.h"
#include "pqueue/http/outbox.h"

#ifndef ARDUINO

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

const std::filesystem::path kApiOutboxSpoolDir = "build/pqueue-spools/api_outbox_client_test_spool";
const std::filesystem::path kDroppedLogDir = "build/spool";
const std::filesystem::path kDroppedLogPath = kDroppedLogDir / "dropped_requests.jsonl";

void cleanTestFiles() {
    std::error_code ec;
    std::filesystem::remove_all(kApiOutboxSpoolDir, ec);
    std::filesystem::remove_all(kDroppedLogDir, ec);
}

struct FakeClock {
    std::uint64_t nowMs = 1000;
};

std::uint64_t fakeClockNow(void* context) {
    return static_cast<FakeClock*>(context)->nowMs;
}

struct PostedRequest {
    std::string url;
    std::vector<pqueue::http::Header> headers;
    std::string body;
};

struct FakeTransport final : pqueue::http::Transport {
    std::vector<pqueue::http::Response> responses;
    std::vector<PostedRequest> posts;

    pqueue::http::Response post(
        const char* url,
        const pqueue::http::Header* headers,
        std::size_t headerCount,
        const std::uint8_t* body,
        std::size_t bodySize
    ) override {
        PostedRequest request;
        request.url = url == nullptr ? std::string{} : std::string{url};
        for (std::size_t i = 0; i < headerCount; ++i) {
            request.headers.push_back(headers[i]);
        }
        request.body.assign(reinterpret_cast<const char*>(body), bodySize);
        posts.push_back(request);

        if (responses.empty()) {
            return {200, pqueue::http::TransportError::None, "{\"status\":\"ok\",\"result\":\"created\"}"};
        }

        const auto response = responses.front();
        responses.erase(responses.begin());
        return response;
    }
};

Config testConfig() {
    Config config;
    config.api.baseUrl = "https://example.test/api";
    config.api.apiKey = "secret-key";
    config.api.pemFile = "/laptop.pem";
    config.api.outbox.diskReserveBytes = 64 * 1024;
    config.api.outbox.drainRateCap = 10;
    config.api.outbox.retryDelayMs = 1000;
    config.api.outbox.logLevel = api::PqueueLogLevel::None;
    return config;
}

SensorIdentity identity() {
    return SensorIdentity{"AA:BB:CC:DD:EE:FF", "Kitchen", "K"};
}

SwitchbotReading validSwitchbotReading() {
    SwitchbotReading reading;
    reading.temperatureC = 21.5f;
    reading.humidityPct = static_cast<std::uint8_t>(56);
    reading.lastSeenEpochS = 1710000000;
    return reading;
}

XiaomiReading validXiaomiReading() {
    XiaomiReading reading;
    reading.temperatureC = 18.25f;
    reading.moisturePct = static_cast<std::uint8_t>(42);
    reading.lastSeenEpochS = 1710000000;
    return reading;
}

api::OutboxClient makeClient(const Config& config, FakeTransport& transport, FakeClock& clock) {
    return api::OutboxClient(config, transport, kApiOutboxSpoolDir.string(), fakeClockNow, &clock);
}

void writeActiveQueueLockFile() {
    std::filesystem::create_directories(kApiOutboxSpoolDir);
    std::ofstream lock(kApiOutboxSpoolDir / ".pqueue.lock", std::ios::binary | std::ios::trunc);
    REQUIRE(lock.good());
    lock << "pqueue-lock-v1\n";
    lock << "pid=" << static_cast<long>(::getpid()) << "\n";
    lock << "token=active-api-outbox-client-test-lock\n";
}

StaticJsonDocument<512> parseBody(const PostedRequest& request) {
    StaticJsonDocument<512> doc;
    const auto err = deserializeJson(doc, request.body);
    REQUIRE_FALSE(err);
    return doc;
}

std::string droppedLogText() {
    std::ifstream in(kDroppedLogPath);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("api outbox client sends valid SwitchBot reading directly") {
    cleanTestFiles();
    const auto config = testConfig();
    FakeTransport transport;
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    const auto result = client.postSwitchbotReading(identity(), validSwitchbotReading());

    CHECK(result.status == api::WriteStatus::Sent);
    CHECK(result.backendResult == api::BackendWriteResult::Created);
    CHECK_EQ(result.httpStatusCode, 200);
    REQUIRE_EQ(transport.posts.size(), 1U);
    CHECK_EQ(transport.posts[0].url, "https://example.test/api/switchbot/reading");

    const auto doc = parseBody(transport.posts[0]);
    CHECK_EQ(doc["mac"].as<std::string>(), "AA:BB:CC:DD:EE:FF");
    CHECK_EQ(doc["type"].as<std::string>(), "switchbot");
    CHECK_EQ(doc["temperature_c"].as<float>(), doctest::Approx(21.5f));
}

TEST_CASE("api outbox client rejects invalid SwitchBot reading before transport") {
    cleanTestFiles();
    const auto config = testConfig();
    FakeTransport transport;
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    auto reading = validSwitchbotReading();
    reading.temperatureC = std::nullopt;

    const auto result = client.postSwitchbotReading(identity(), reading);

    CHECK(result.status == api::WriteStatus::DroppedPermanent);
    CHECK_EQ(transport.posts.size(), 0U);
}

TEST_CASE("api outbox client queues retryable failure and skips direct send while backlog exists") {
    cleanTestFiles();
    const auto config = testConfig();
    FakeTransport transport;
    transport.responses.push_back({pqueue::http::kNoStatusCode, pqueue::http::TransportError::Network});
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    const auto first = client.postSwitchbotReading(identity(), validSwitchbotReading());
    CHECK(first.status == api::WriteStatus::Queued);
    CHECK(first.queueReason == api::WriteQueueReason::RetryableFailure);
    REQUIRE_EQ(transport.posts.size(), 1U);

    const auto second = client.postXiaomiReading(identity(), validXiaomiReading());
    CHECK(second.status == api::WriteStatus::Queued);
    CHECK(second.queueReason == api::WriteQueueReason::BacklogPresent);
    CHECK_EQ(transport.posts.size(), 1U);
}

TEST_CASE("api outbox client reports retryable send that cannot be queued as temporary failure") {
    cleanTestFiles();
    const auto config = testConfig();
    writeActiveQueueLockFile();

    FakeTransport transport;
    transport.responses.push_back({pqueue::http::kNoStatusCode, pqueue::http::TransportError::Network});
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    const auto result = client.postSwitchbotReading(identity(), validSwitchbotReading());

    CHECK(result.status == api::WriteStatus::FailedTemporary);
    CHECK_EQ(result.httpStatusCode, 0);
    REQUIRE_EQ(transport.posts.size(), 1U);
}

TEST_CASE("api outbox client drains queued backlog after retry delay") {
    cleanTestFiles();
    auto config = testConfig();
    config.api.outbox.retryDelayMs = 1000;
    FakeTransport transport;
    transport.responses.push_back({pqueue::http::kNoStatusCode, pqueue::http::TransportError::Network});
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    REQUIRE(client.postSwitchbotReading(identity(), validSwitchbotReading()).status == api::WriteStatus::Queued);
    REQUIRE(client.postXiaomiReading(identity(), validXiaomiReading()).status == api::WriteStatus::Queued);
    REQUIRE_EQ(transport.posts.size(), 1U);

    clock.nowMs += 1000;
    const auto drained = client.drainPending(clock.nowMs);

    CHECK_EQ(drained.attempted, 2);
    CHECK_EQ(drained.sent, 2);
    CHECK_EQ(drained.dropped, 0);
    REQUIRE_EQ(transport.posts.size(), 3U);
    CHECK_EQ(transport.posts[1].url, "https://example.test/api/switchbot/reading");
    CHECK_EQ(transport.posts[2].url, "https://example.test/api/xiaomi/reading");
}

TEST_CASE("api outbox client drainPending respects drainRateCap per second") {
    cleanTestFiles();
    auto config = testConfig();
    config.api.outbox.drainRateCap = 2;
    config.api.outbox.retryDelayMs = 0;

    FakeTransport transport;
    transport.responses.push_back({pqueue::http::kNoStatusCode, pqueue::http::TransportError::Network});
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    REQUIRE(client.postSwitchbotReading(identity(), validSwitchbotReading()).status == api::WriteStatus::Queued);
    REQUIRE(client.postXiaomiReading(identity(), validXiaomiReading()).status == api::WriteStatus::Queued);
    REQUIRE(client.postSwitchbotReading(identity(), validSwitchbotReading()).status == api::WriteStatus::Queued);
    REQUIRE_EQ(transport.posts.size(), 1U);

    const auto firstDrain = client.drainPending(clock.nowMs);
    CHECK_EQ(firstDrain.attempted, 2);
    CHECK_EQ(firstDrain.sent, 2);
    CHECK_EQ(firstDrain.dropped, 0);
    CHECK_FALSE(firstDrain.notDueYet);
    CHECK_FALSE(firstDrain.blockedByRetryableFailure);
    REQUIRE_EQ(transport.posts.size(), 3U);

    const auto cappedDrain = client.drainPending(clock.nowMs);
    CHECK_EQ(cappedDrain.attempted, 0);
    CHECK_EQ(cappedDrain.sent, 0);
    CHECK_EQ(cappedDrain.dropped, 0);
    CHECK(cappedDrain.notDueYet);
    CHECK_FALSE(cappedDrain.blockedByRetryableFailure);
    CHECK_EQ(transport.posts.size(), 3U);

    clock.nowMs += 1000;
    const auto secondWindowDrain = client.drainPending(clock.nowMs);
    CHECK_EQ(secondWindowDrain.attempted, 1);
    CHECK_EQ(secondWindowDrain.sent, 1);
    CHECK_EQ(secondWindowDrain.dropped, 0);
    CHECK_FALSE(secondWindowDrain.notDueYet);
    CHECK_FALSE(secondWindowDrain.blockedByRetryableFailure);
    REQUIRE_EQ(transport.posts.size(), 4U);
    CHECK_EQ(transport.posts[3].url, "https://example.test/api/switchbot/reading");
}

TEST_CASE("api outbox client drops permanent backend rejection and writes dropped log") {
    cleanTestFiles();
    std::filesystem::create_directories(kDroppedLogDir);
    const auto config = testConfig();
    FakeTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None, "{\"status\":\"error\"}"});
    FakeClock clock;
    auto client = makeClient(config, transport, clock);

    const auto result = client.postSwitchbotReading(identity(), validSwitchbotReading());

    CHECK(result.status == api::WriteStatus::DroppedPermanent);
    CHECK_EQ(result.httpStatusCode, 422);
    REQUIRE_EQ(transport.posts.size(), 1U);

    const auto log = droppedLogText();
    CHECK(log.find("classified_drop") != std::string::npos);
    CHECK(log.find("/switchbot/reading") != std::string::npos);
    CHECK(log.find("AA:BB:CC:DD:EE:FF") != std::string::npos);
}

TEST_CASE("api outbox client resolves absolute PEM config path under desktop data directory") {
    CHECK_EQ(api::detail::resolveDesktopPemPathForApiOutbox("/laptop.pem"), "data/laptop.pem");
    CHECK_EQ(api::detail::resolveDesktopPemPathForApiOutbox("data/custom.pem"), "data/custom.pem");
}

#endif // !ARDUINO
