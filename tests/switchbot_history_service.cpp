#include "switchbot/history_service.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace switchbot::history;

constexpr std::uint32_t kNow = 1735689600U; // 2026-01-01T00:00:00Z
constexpr std::uint32_t k15Min = 900U;

class FakeSession : public ISensorHistorySession {
public:
    SyncResult openResult;
    std::vector<SyncResult> fetchResults;
    std::size_t fetchCount = 0;
    std::vector<SyncRequest>* fetchLog = nullptr;

    SyncResult open() override { return openResult; }

    SyncResult fetch(const SyncRequest& req) override {
        if (fetchLog) fetchLog->push_back(req);
        if (fetchCount < fetchResults.size()) {
            return fetchResults[fetchCount++];
        }
        SyncResult r;
        r.status = SyncStatus::Timeout;
        r.message = "no more fake fetch results";
        return r;
    }
};

std::vector<Sample> makeSamplesAt(std::uint32_t startEpoch, std::uint32_t count,
                                  std::uint32_t interval = k15Min) {
    std::vector<Sample> samples;
    for (std::uint32_t i = 0; i < count; ++i) {
        Sample s;
        s.epoch = startEpoch + i * interval;
        s.temperatureC = 20.0 + static_cast<double>(i) * 0.1;
        s.humidityPct = 50;
        samples.push_back(s);
    }
    return samples;
}

BulkUploadResult okUpload() {
    BulkUploadResult r;
    r.ok = true;
    return r;
}

SyncResult okFetch(std::vector<Sample> samples) {
    SyncResult r;
    r.status = SyncStatus::Ok;
    r.samples = std::move(samples);
    r.metadata.intervalSeconds = k15Min;
    return r;
}

BackendSensorInfo makeNewSensor(const std::string& mac = "AA:BB:CC:DD:EE:FF",
                                const std::string& id = "sensor-1") {
    BackendSensorInfo s;
    s.mac = mac;
    s.sensorId = id;
    return s;
}

BackendSensorInfo makeTrailingSensor(std::uint32_t latestEpoch,
                                     const std::string& mac = "AA:BB:CC:DD:EE:FF",
                                     const std::string& id = "sensor-1") {
    BackendSensorInfo s;
    s.mac = mac;
    s.sensorId = id;
    s.latestEpoch = latestEpoch;
    s.latestTimestamp = formatIsoUtc(latestEpoch);
    return s;
}

HistoryServiceOptions testOptions(std::uint32_t windowSeconds = 3600) {
    HistoryServiceOptions o;
    o.newSensorWindowSeconds = windowSeconds;
    o.sampleIntervalSeconds = k15Min;
    o.historyLimitSeconds = 68U * 24U * 3600U;
    o.bulkBatchLimit = 1000;
    o.commandTimeoutMs = 3000;
    return o;
}

struct Recorded {
    std::vector<std::string> sessionsCreated;
    std::vector<SyncRequest> fetchRequests;
    std::vector<std::vector<BulkHistoryReading>> uploads;
};

HistoryServiceDeps buildDeps(Recorded& rec,
                             SensorLookupResult lookup,
                             SyncResult openResult = SyncResult{},
                             std::vector<SyncResult> fetches = {},
                             BulkUploadResult uploadResult = okUpload()) {
    HistoryServiceDeps d;
    d.sensorLookup = [lookup](const std::vector<std::string>&) { return lookup; };
    d.sessionFactory = [&rec, openResult, fetches](const std::string& mac)
            -> std::unique_ptr<ISensorHistorySession> {
        rec.sessionsCreated.push_back(mac);
        auto s = std::make_unique<FakeSession>();
        s->openResult = openResult;
        s->fetchResults = fetches;
        s->fetchLog = &rec.fetchRequests;
        return s;
    };
    d.bulkUpload = [&rec, uploadResult](const std::string&,
                                        const std::vector<BulkHistoryReading>& r) {
        rec.uploads.push_back(r);
        return uploadResult;
    };
    return d;
}

SensorLookupResult okLookup(std::vector<BackendSensorInfo> sensors) {
    SensorLookupResult r;
    r.ok = true;
    r.sensors = std::move(sensors);
    return r;
}

const std::map<std::string, std::string> kLabels{{"AA:BB:CC:DD:EE:FF", "sensor"}};

} // namespace

TEST_CASE("history service: lookup failure creates no sessions") {
    SensorLookupResult failed;
    failed.ok = false;
    failed.error = "network error";

    Recorded rec;
    auto deps = buildDeps(rec, failed);
    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    CHECK(rec.sessionsCreated.empty());
    CHECK(rec.uploads.empty());
}

TEST_CASE("history service: up-to-date sensor creates no session") {
    Recorded rec;
    auto deps = buildDeps(rec, okLookup({makeTrailingSensor(kNow)}));
    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    CHECK(rec.sessionsCreated.empty());
    CHECK(rec.uploads.empty());
}

TEST_CASE("history service: new sensor uploads 15-min aligned readings") {
    // 1h window → 4 points at 0, 15, 30, 45 min before kNow
    const std::uint32_t windowStart = kNow - 3600; // 1735686000, divisible by 900
    const std::uint32_t expectedCount = 4;

    Recorded rec;
    auto deps = buildDeps(rec,
                          okLookup({makeNewSensor()}),
                          SyncResult{},
                          {okFetch(makeSamplesAt(windowStart, expectedCount))});

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(3600), deps);

    REQUIRE(rec.sessionsCreated.size() == 1);
    CHECK_EQ(rec.sessionsCreated[0], "AA:BB:CC:DD:EE:FF");
    REQUIRE(rec.uploads.size() == 1);
    CHECK_EQ(rec.uploads[0].size(), expectedCount);
    for (const auto& r : rec.uploads[0]) {
        CHECK_EQ(r.timestampEpoch % k15Min, 0U);
    }
}

TEST_CASE("history service: trailing catch-up syncs from latest to now") {
    const std::uint32_t latestEpoch = kNow - 3600; // 1735686000, divisible by 900
    const std::uint32_t expectedCount = 4;

    Recorded rec;
    auto deps = buildDeps(rec,
                          okLookup({makeTrailingSensor(latestEpoch)}),
                          SyncResult{},
                          {okFetch(makeSamplesAt(latestEpoch, expectedCount))});

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    REQUIRE(rec.uploads.size() == 1);
    CHECK_EQ(rec.uploads[0].size(), expectedCount);
    CHECK_EQ(rec.uploads[0].front().timestampEpoch, latestEpoch);
    CHECK_EQ(rec.uploads[0].back().timestampEpoch, latestEpoch + 3 * k15Min);
}

TEST_CASE("history service: readings over batch limit are split across uploads") {
    auto opts = testOptions(3600); // 4 points in window
    opts.bulkBatchLimit = 2;       // split into 2 batches of 2

    const std::uint32_t windowStart = kNow - 3600;

    Recorded rec;
    auto deps = buildDeps(rec,
                          okLookup({makeNewSensor()}),
                          SyncResult{},
                          {okFetch(makeSamplesAt(windowStart, 4))});

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, opts, deps);

    REQUIRE(rec.uploads.size() == 2);
    CHECK_EQ(rec.uploads[0].size(), 2U);
    CHECK_EQ(rec.uploads[1].size(), 2U);
}

TEST_CASE("history service: session open failure skips all windows for that sensor") {
    SyncResult connectFailed;
    connectFailed.status = SyncStatus::ConnectFailed;
    connectFailed.message = "BLE connect failed";

    Recorded rec;
    auto deps = buildDeps(rec, okLookup({makeNewSensor()}), connectFailed);
    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    CHECK_EQ(rec.sessionsCreated.size(), 1U);
    CHECK(rec.uploads.empty());
}

TEST_CASE("history service: second sensor still syncs when first fails to open") {
    const std::string mac1 = "AA:BB:CC:DD:EE:01";
    const std::string mac2 = "AA:BB:CC:DD:EE:02";
    const std::uint32_t windowStart = kNow - 3600;

    SensorLookupResult lookup = okLookup({makeNewSensor(mac1, "s1"), makeNewSensor(mac2, "s2")});

    Recorded rec;
    HistoryServiceDeps deps;
    deps.sensorLookup = [&](const std::vector<std::string>&) { return lookup; };
    deps.sessionFactory = [&](const std::string& mac) -> std::unique_ptr<ISensorHistorySession> {
        rec.sessionsCreated.push_back(mac);
        auto s = std::make_unique<FakeSession>();
        if (mac == mac1) {
            s->openResult.status = SyncStatus::ConnectFailed;
            s->openResult.message = "BLE failed";
        } else {
            s->fetchResults.push_back(okFetch(makeSamplesAt(windowStart, 4)));
        }
        return s;
    };
    deps.bulkUpload = [&](const std::string&, const std::vector<BulkHistoryReading>& r) {
        rec.uploads.push_back(r);
        return okUpload();
    };

    const std::map<std::string, std::string> labels{{mac1, "Sensor 1"}, {mac2, "Sensor 2"}};
    runHistorySync({mac1, mac2}, labels, kNow, testOptions(), deps);

    CHECK_EQ(rec.sessionsCreated.size(), 2U);
    REQUIRE(rec.uploads.size() == 1);
    CHECK_EQ(rec.uploads[0].size(), 4U);
}

TEST_CASE("history service: failed window fetch still uploads any partial readings") {
    const std::uint32_t windowStart = kNow - 3600;

    // The fetch returns samples but also reports an error status
    SyncResult partialFetch = okFetch(makeSamplesAt(windowStart, 2));
    partialFetch.status = SyncStatus::Timeout;
    partialFetch.message = "timed out mid-fetch";

    Recorded rec;
    auto deps = buildDeps(rec,
                          okLookup({makeNewSensor()}),
                          SyncResult{},
                          {partialFetch});

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    // Partial readings should still be uploaded even on sync failure
    REQUIRE(rec.uploads.size() == 1);
    CHECK_EQ(rec.uploads[0].size(), 2U);
}

TEST_CASE("history service: internal gaps each trigger a separate fetch and upload") {
    // Each syncInterval IS a gap the backend needs filled — planHistoryWindows maps each
    // directly to an internal_gap window, so two intervals → two fetch calls.
    const std::uint32_t gap1Start = kNow - 7200; // 2h ago
    const std::uint32_t gap1End   = kNow - 6300; // 1h45m ago  → 1 point at gap1Start (if aligned)
    const std::uint32_t gap2Start = kNow - 3600; // 1h ago
    const std::uint32_t gap2End   = kNow - 2700; // 45m ago    → 1 point at gap2Start (if aligned)

    BackendSensorInfo sensor;
    sensor.mac = "AA:BB:CC:DD:EE:FF";
    sensor.sensorId = "sensor-1";
    sensor.latestEpoch = kNow; // trailing window would be empty (latestEpoch == now)
    sensor.latestTimestamp = formatIsoUtc(kNow);
    BackendSyncInterval i1;
    i1.startEpoch = gap1Start;
    i1.endEpoch   = gap1End;
    BackendSyncInterval i2;
    i2.startEpoch = gap2Start;
    i2.endEpoch   = gap2End;
    sensor.syncIntervals = {i1, i2};

    Recorded rec;
    auto deps = buildDeps(rec,
                          okLookup({sensor}),
                          SyncResult{},
                          {okFetch(makeSamplesAt(gap1Start, 1)),
                           okFetch(makeSamplesAt(gap2Start, 1))});

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, testOptions(), deps);

    // One session, two fetches, two uploads (one per gap)
    // Requests are expanded ±60s by syncAndUploadWindow before being sent to fetch().
    REQUIRE(rec.sessionsCreated.size() == 1);
    REQUIRE(rec.fetchRequests.size() == 2U);
    CHECK_EQ(rec.fetchRequests[0].startEpoch, gap1Start - 60);
    CHECK_EQ(rec.fetchRequests[0].endEpoch,   gap1End   + 60);
    CHECK_EQ(rec.fetchRequests[1].startEpoch, gap2Start - 60);
    CHECK_EQ(rec.fetchRequests[1].endEpoch,   gap2End   + 60);
    REQUIRE(rec.uploads.size() == 2);
    CHECK_EQ(rec.uploads[0][0].timestampEpoch, gap1Start);
    CHECK_EQ(rec.uploads[1][0].timestampEpoch, gap2Start);
}

TEST_CASE("history service: upload failure is recorded but later batches still run") {
    auto opts = testOptions(3600); // 4 readings in window
    opts.bulkBatchLimit = 2;       // 2 batches: first fails, second succeeds

    const std::uint32_t windowStart = kNow - 3600;

    int callCount = 0;
    Recorded rec;
    HistoryServiceDeps deps;
    deps.sensorLookup = [&](const std::vector<std::string>&) {
        return okLookup({makeNewSensor()});
    };
    deps.sessionFactory = [&](const std::string& mac) -> std::unique_ptr<ISensorHistorySession> {
        rec.sessionsCreated.push_back(mac);
        auto s = std::make_unique<FakeSession>();
        s->fetchResults.push_back(okFetch(makeSamplesAt(windowStart, 4)));
        return s;
    };
    deps.bulkUpload = [&](const std::string&, const std::vector<BulkHistoryReading>& r) {
        rec.uploads.push_back(r);
        BulkUploadResult result;
        result.ok = (callCount++ > 0); // first call fails, second succeeds
        result.error = result.ok ? "" : "server error";
        return result;
    };

    runHistorySync({"AA:BB:CC:DD:EE:FF"}, kLabels, kNow, opts, deps);

    // Both batches attempted — no early exit on failure
    CHECK_EQ(rec.uploads.size(), 2U);
    CHECK_EQ(rec.uploads[0].size(), 2U);
    CHECK_EQ(rec.uploads[1].size(), 2U);
}
