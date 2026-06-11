#include "api_sync.h"

#include "api/backend_result.h"
#include "api/outbox_client.h"
#include "api/state.h"
#include "doctest/doctest.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace {

constexpr std::int64_t kSeenAt = 1'710'000'000;
constexpr std::int64_t kNow = kSeenAt + 10;

struct SwitchbotPost {
    SensorIdentity identity;
    SwitchbotReading reading;
};

api::WriteResult writeResult(
    api::WriteStatus status,
    api::BackendWriteResult backendResult = api::BackendWriteResult::Failed
) {
    api::WriteResult result;
    result.status = status;
    result.backendResult = backendResult;
    result.httpStatusCode = status == api::WriteStatus::Sent ? 200 : 0;
    result.body = backendResult == api::BackendWriteResult::Conflict ? "conflict" : "";
    return result;
}

class FakeApiWriter final : public api::ApiWriter {
public:
    api::WriteResult switchbotResult = writeResult(
        api::WriteStatus::Sent,
        api::BackendWriteResult::Created
    );
    std::vector<SwitchbotPost> switchbotPosts;

    api::WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    ) override {
        switchbotPosts.push_back(SwitchbotPost{identity, reading});
        return switchbotResult;
    }
};

SensorIdentity switchbotIdentity() {
    return SensorIdentity{"AA:BB:CC:DD:EE:01", "SwitchBot", "SB"};
}

SwitchbotReading switchbotReading(
    float temperatureC = 21.5f,
    std::uint8_t humidityPct = 56,
    std::int64_t seenAt = kSeenAt
) {
    SwitchbotReading reading;
    reading.temperatureC = temperatureC;
    reading.humidityPct = humidityPct;
    reading.lastSeenEpochS = seenAt;
    return reading;
}

State appStateWithSwitchbot(const SwitchbotReading& reading) {
    State appState;
    appState.switchbotSensors.push_back(SwitchbotSensorState{switchbotIdentity(), reading});
    return appState;
}

api::State apiStateFor(const State& appState) {
    api::State state;
    api::initState(appState, state);
    return state;
}

void sync(
    const State& appState,
    api::State& apiState,
    FakeApiWriter& writer,
    std::int64_t now = kNow
) {
    const Config config{};
    syncApiState(config, appState, apiState, writer, now);
}

} // namespace

TEST_CASE("api sync switchbot accepted write updates last sent") {
    const auto reading = switchbotReading();
    const auto appState = appStateWithSwitchbot(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;
    writer.switchbotResult = writeResult(api::WriteStatus::Sent, api::BackendWriteResult::Created);

    sync(appState, apiState, writer);

    REQUIRE_EQ(writer.switchbotPosts.size(), 1U);
    CHECK_EQ(writer.switchbotPosts[0].reading.temperatureC, reading.temperatureC);
    CHECK(apiState.switchbot.lastSent[0].equalsForApi(reading));
}

TEST_CASE("api sync switchbot queued write updates last sent") {
    const auto reading = switchbotReading();
    const auto appState = appStateWithSwitchbot(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;
    writer.switchbotResult = writeResult(api::WriteStatus::Queued);

    sync(appState, apiState, writer);

    REQUIRE_EQ(writer.switchbotPosts.size(), 1U);
    CHECK(apiState.switchbot.lastSent[0].equalsForApi(reading));
}

TEST_CASE("api sync switchbot dropped write leaves last sent unchanged") {
    const auto reading = switchbotReading();
    const auto appState = appStateWithSwitchbot(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;
    writer.switchbotResult = writeResult(api::WriteStatus::DroppedPermanent);

    sync(appState, apiState, writer);

    REQUIRE_EQ(writer.switchbotPosts.size(), 1U);
    CHECK_FALSE(apiState.switchbot.lastSent[0].hasAnyValue());
}

TEST_CASE("api sync switchbot conflict clears last sent") {
    const auto reading = switchbotReading(22.0f, 58, kSeenAt + 120);
    const auto appState = appStateWithSwitchbot(reading);
    auto apiState = apiStateFor(appState);
    apiState.switchbot.lastSent[0] = switchbotReading(21.0f, 55, kSeenAt);
    FakeApiWriter writer;
    writer.switchbotResult = writeResult(api::WriteStatus::Sent, api::BackendWriteResult::Conflict);

    sync(appState, apiState, writer);

    REQUIRE_EQ(writer.switchbotPosts.size(), 1U);
    CHECK_FALSE(apiState.switchbot.lastSent[0].hasAnyValue());
}

TEST_CASE("api sync switchbot invalid timestamp does not post") {
    auto reading = switchbotReading();
    reading.lastSeenEpochS = std::nullopt;
    const auto appState = appStateWithSwitchbot(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;

    sync(appState, apiState, writer);

    CHECK(writer.switchbotPosts.empty());
    CHECK_FALSE(apiState.switchbot.lastSent[0].hasAnyValue());
}
