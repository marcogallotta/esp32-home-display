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

struct XiaomiPost {
    SensorIdentity identity;
    XiaomiReading reading;
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
    api::WriteResult xiaomiResult = writeResult(
        api::WriteStatus::Sent,
        api::BackendWriteResult::Created
    );
    std::vector<SwitchbotPost> switchbotPosts;
    std::vector<XiaomiPost> xiaomiPosts;

    api::WriteResult postSwitchbotReading(
        const SensorIdentity& identity,
        const SwitchbotReading& reading
    ) override {
        switchbotPosts.push_back(SwitchbotPost{identity, reading});
        return switchbotResult;
    }

    api::WriteResult postXiaomiReading(
        const SensorIdentity& identity,
        const XiaomiReading& reading
    ) override {
        xiaomiPosts.push_back(XiaomiPost{identity, reading});
        return xiaomiResult;
    }
};

SensorIdentity switchbotIdentity() {
    return SensorIdentity{"AA:BB:CC:DD:EE:01", "SwitchBot", "SB"};
}

SensorIdentity xiaomiIdentity() {
    return SensorIdentity{"AA:BB:CC:DD:EE:02", "Plant", "PL"};
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

XiaomiReading partialXiaomiReading(std::int64_t seenAt = kSeenAt) {
    XiaomiReading reading;
    reading.temperatureC = 18.25f;
    reading.lastSeenEpochS = seenAt;
    return reading;
}

XiaomiReading completeXiaomiReading(std::int64_t seenAt = kSeenAt) {
    XiaomiReading reading;
    reading.temperatureC = 18.25f;
    reading.moisturePct = static_cast<std::uint8_t>(42);
    reading.lux = 1234;
    reading.conductivityUsCm = 567;
    reading.lastSeenEpochS = seenAt;
    return reading;
}

State appStateWithSwitchbot(const SwitchbotReading& reading) {
    State appState;
    appState.switchbotSensors.push_back(SwitchbotSensorState{switchbotIdentity(), reading});
    return appState;
}

State appStateWithXiaomi(const XiaomiReading& reading) {
    State appState;
    appState.xiaomiSensors.push_back(XiaomiSensorState{xiaomiIdentity(), reading});
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

TEST_CASE("api sync xiaomi partial reading opens pending without posting") {
    const auto reading = partialXiaomiReading();
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;

    sync(appState, apiState, writer, kSeenAt + 30);

    CHECK(writer.xiaomiPosts.empty());
    REQUIRE(apiState.xiaomi.pending[0].active);
    CHECK_EQ(apiState.xiaomi.pending[0].openedAtEpochS, kSeenAt);
    CHECK(apiState.xiaomi.pending[0].reading.equalsForApi(reading));
    CHECK_FALSE(apiState.xiaomi.lastSent[0].hasAnyValue());
}

TEST_CASE("api sync xiaomi complete reading flushes immediately") {
    const auto reading = completeXiaomiReading();
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;

    sync(appState, apiState, writer, kSeenAt + 1);

    REQUIRE_EQ(writer.xiaomiPosts.size(), 1U);
    CHECK(writer.xiaomiPosts[0].reading.equalsForApi(reading));
    CHECK(apiState.xiaomi.lastSent[0].equalsForApi(reading));
    CHECK_FALSE(apiState.xiaomi.pending[0].active);
}

TEST_CASE("api sync xiaomi partial reading flushes after pending window") {
    const auto reading = partialXiaomiReading();
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;

    sync(appState, apiState, writer, kSeenAt + 60);

    REQUIRE_EQ(writer.xiaomiPosts.size(), 1U);
    CHECK(writer.xiaomiPosts[0].reading.equalsForApi(reading));
    CHECK(apiState.xiaomi.lastSent[0].equalsForApi(reading));
    CHECK_FALSE(apiState.xiaomi.pending[0].active);
}

TEST_CASE("api sync xiaomi queued write updates last sent and clears pending") {
    const auto reading = completeXiaomiReading();
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;
    writer.xiaomiResult = writeResult(api::WriteStatus::Queued);

    sync(appState, apiState, writer, kSeenAt + 1);

    REQUIRE_EQ(writer.xiaomiPosts.size(), 1U);
    CHECK(apiState.xiaomi.lastSent[0].equalsForApi(reading));
    CHECK_FALSE(apiState.xiaomi.pending[0].active);
}

TEST_CASE("api sync xiaomi dropped write keeps pending for retry") {
    const auto reading = completeXiaomiReading();
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    FakeApiWriter writer;
    writer.xiaomiResult = writeResult(api::WriteStatus::DroppedPermanent);

    sync(appState, apiState, writer, kSeenAt + 1);

    REQUIRE_EQ(writer.xiaomiPosts.size(), 1U);
    CHECK_FALSE(apiState.xiaomi.lastSent[0].hasAnyValue());
    REQUIRE(apiState.xiaomi.pending[0].active);
    CHECK(apiState.xiaomi.pending[0].reading.equalsForApi(reading));
}

TEST_CASE("api sync xiaomi conflict clears last sent and pending") {
    auto reading = completeXiaomiReading(kSeenAt + 120);
    reading.moisturePct = static_cast<std::uint8_t>(45);
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    apiState.xiaomi.lastSent[0] = completeXiaomiReading(kSeenAt);
    FakeApiWriter writer;
    writer.xiaomiResult = writeResult(api::WriteStatus::Sent, api::BackendWriteResult::Conflict);

    sync(appState, apiState, writer, kSeenAt + 121);

    REQUIRE_EQ(writer.xiaomiPosts.size(), 1U);
    CHECK_FALSE(apiState.xiaomi.lastSent[0].hasAnyValue());
    CHECK_FALSE(apiState.xiaomi.pending[0].active);
}

TEST_CASE("api sync xiaomi invalid timestamp resets pending without posting") {
    auto reading = partialXiaomiReading();
    reading.lastSeenEpochS = std::nullopt;
    const auto appState = appStateWithXiaomi(reading);
    auto apiState = apiStateFor(appState);
    apiState.xiaomi.pending[0].active = true;
    apiState.xiaomi.pending[0].openedAtEpochS = kSeenAt;
    apiState.xiaomi.pending[0].reading = partialXiaomiReading();
    FakeApiWriter writer;

    sync(appState, apiState, writer, kSeenAt + 60);

    CHECK(writer.xiaomiPosts.empty());
    CHECK_FALSE(apiState.xiaomi.pending[0].active);
    CHECK_FALSE(apiState.xiaomi.lastSent[0].hasAnyValue());
}
