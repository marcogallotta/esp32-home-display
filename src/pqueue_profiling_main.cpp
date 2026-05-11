#ifdef ARDUINO

#include <Arduino.h>
#include <LittleFS.h>
#include <memory>
#include <string>

#include "counting_file_system.h"
#include "pqueue/http/outbox.h"
#include "pqueue/outbox.h"
#include "pqueue/queue.h"
#include "pqueue/storage_common.h"

namespace {

static constexpr uint32_t kRecordsPerScenario = 20;
static constexpr uint32_t kRealisticBodyBytes = 152;
static constexpr const char* kBasePath = "/pqueue_prof";

static const uint32_t kRecordSizes[] = {492};

struct DeviceTimings {
    uint64_t sumUs = 0;
    uint64_t minUs = UINT64_MAX;
    uint64_t maxUs = 0;
    uint32_t count = 0;

    void add(uint64_t us) {
        sumUs += us;
        if (us < minUs) minUs = us;
        if (us > maxUs) maxUs = us;
        ++count;
    }

    uint64_t avg() const { return count > 0 ? sumUs / count : 0; }
};

struct ScenarioResult {
    const char* name = "";
    uint32_t recordSizeBytes = 0;
    DeviceTimings timings;
    FsCounters fs;
    bool failed = false;
    const char* failReason = "";
};

std::shared_ptr<CountingFileSystem> makeCfs() {
    return std::make_shared<CountingFileSystem>(pqueue::makeLittleFsFileSystem());
}

pqueue::Config makeQueueConfig(std::shared_ptr<CountingFileSystem> fs, uint32_t recordSizeBytes) {
    pqueue::Config cfg;
    cfg.basePath = kBasePath;
    cfg.recordSizeBytes = recordSizeBytes;
    cfg.reservedBytes = (kRecordsPerScenario + 8) *
        static_cast<uint32_t>(pqueue::storage_detail::kRecordHeaderBytes + recordSizeBytes);
    cfg.storageBackend = pqueue::StorageBackend::LittleFS;
    cfg.fileSystem = fs;
    return cfg;
}

std::string makePayload(uint32_t size) {
    std::string out(size, 'x');
    for (uint32_t i = 0; i < size; ++i) {
        out[i] = static_cast<char>('a' + (i % 26));
    }
    return out;
}

bool reformatLittleFS() {
    if (!LittleFS.format()) {
        Serial.println("ERROR: LittleFS.format() failed");
        return false;
    }
    if (!LittleFS.begin()) {
        Serial.println("ERROR: LittleFS.begin() after format failed");
        return false;
    }
    return true;
}

ScenarioResult runQueueEnqueue(uint32_t recordSizeBytes) {
    ScenarioResult result;
    result.name = "queue_enqueue";
    result.recordSizeBytes = recordSizeBytes;

    if (!reformatLittleFS()) { result.failed = true; result.failReason = "format"; return result; }

    auto fs = makeCfs();
    pqueue::Queue q(makeQueueConfig(fs, recordSizeBytes));
    (void)q.stats();
    const auto data = makePayload(recordSizeBytes);
    fs->resetCounters();

    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        const int64_t t0 = esp_timer_get_time();
        const auto st = q.enqueue(data);
        const uint64_t us = static_cast<uint64_t>(esp_timer_get_time() - t0);
        result.timings.add(us);
        if (!st.ok()) { result.failed = true; result.failReason = st.message; break; }
    }
    result.fs = fs->counters();
    return result;
}

ScenarioResult runQueuePeekPop(uint32_t recordSizeBytes) {
    ScenarioResult result;
    result.name = "queue_peek_pop";
    result.recordSizeBytes = recordSizeBytes;

    if (!reformatLittleFS()) { result.failed = true; result.failReason = "format"; return result; }

    auto fs = makeCfs();
    pqueue::Queue q(makeQueueConfig(fs, recordSizeBytes));
    const auto data = makePayload(recordSizeBytes);
    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        if (!q.enqueue(data).ok()) { result.failed = true; result.failReason = "setup_enqueue"; return result; }
    }
    fs->resetCounters();

    std::string out;
    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        const int64_t t0 = esp_timer_get_time();
        auto st = q.peek(out);
        if (st.ok()) st = q.pop();
        const uint64_t us = static_cast<uint64_t>(esp_timer_get_time() - t0);
        result.timings.add(us);
        if (!st.ok()) { result.failed = true; result.failReason = st.message; break; }
    }
    result.fs = fs->counters();
    return result;
}

struct FakeTransport final : public pqueue::http::Transport {
    bool online = true;

    pqueue::http::Response post(
        const char*,
        const pqueue::http::Header*,
        std::size_t,
        const std::uint8_t*,
        std::size_t
    ) override {
        if (!online) {
            return {pqueue::http::kNoStatusCode, pqueue::http::TransportError::Network};
        }
        return {200, pqueue::http::TransportError::None};
    }
};

struct FakeClock {
    std::uint64_t now = 0;
};

std::uint64_t fakeClockCb(void* ctx) {
    return static_cast<FakeClock*>(ctx)->now;
}

pqueue::http::Config makeHttpConfig(std::shared_ptr<CountingFileSystem> fs, uint32_t recordSizeBytes) {
    pqueue::http::Config cfg;
    cfg.queue = makeQueueConfig(fs, recordSizeBytes);
    cfg.outbox.maxDrainAttemptsPerSecond = 1000;
    cfg.outbox.retryDelayMs = 1;
    cfg.baseUrl = "https://example.test";
    return cfg;
}

ScenarioResult runHttpOfflineSubmit(uint32_t recordSizeBytes) {
    ScenarioResult result;
    result.name = "http_offline_submit";
    result.recordSizeBytes = recordSizeBytes;

    if (!reformatLittleFS()) { result.failed = true; result.failReason = "format"; return result; }

    auto fs = makeCfs();
    FakeTransport transport;
    FakeClock clock;
    pqueue::http::Outbox outbox(makeHttpConfig(fs, recordSizeBytes), transport, fakeClockCb, &clock);
    (void)outbox.stats();
    const auto body = makePayload(kRealisticBodyBytes);
    transport.online = false;
    fs->resetCounters();

    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        const int64_t t0 = esp_timer_get_time();
        const auto submit = outbox.submitPost("/readings", body);
        const uint64_t us = static_cast<uint64_t>(esp_timer_get_time() - t0);
        result.timings.add(us);
        if (submit.status != pqueue::SubmitStatus::Queued) {
            result.failed = true; result.failReason = submit.detail.message; break;
        }
        clock.now += 2;
    }
    result.fs = fs->counters();
    return result;
}

ScenarioResult runHttpDrainBacklog(uint32_t recordSizeBytes) {
    ScenarioResult result;
    result.name = "http_drain_backlog";
    result.recordSizeBytes = recordSizeBytes;

    if (!reformatLittleFS()) { result.failed = true; result.failReason = "format"; return result; }

    auto fs = makeCfs();
    FakeTransport transport;
    FakeClock clock;
    pqueue::http::Outbox outbox(makeHttpConfig(fs, recordSizeBytes), transport, fakeClockCb, &clock);
    const auto body = makePayload(kRealisticBodyBytes);
    transport.online = false;
    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        const auto submit = outbox.submitPost("/readings", body);
        if (submit.status != pqueue::SubmitStatus::Queued) {
            result.failed = true; result.failReason = "setup_submit"; return result;
        }
        clock.now += 2;
    }

    transport.online = true;
    clock.now += 100000;
    fs->resetCounters();

    for (uint32_t i = 0; i < kRecordsPerScenario; ++i) {
        const int64_t t0 = esp_timer_get_time();
        const auto drain = outbox.drainUpTo(1);
        const uint64_t us = static_cast<uint64_t>(esp_timer_get_time() - t0);
        result.timings.add(us);
        if (drain.queueError || drain.sendError || drain.sent != 1) {
            result.failed = true; result.failReason = drain.detail.message; break;
        }
        clock.now += 2;
    }
    result.fs = fs->counters();
    return result;
}

void printResult(const ScenarioResult& r) {
    Serial.printf("%-22s %4uB  avg=%6llu us  min=%6llu us  max=%6llu us\n",
        r.name, r.recordSizeBytes, r.timings.avg(),
        r.timings.count > 0 ? r.timings.minUs : 0ULL, r.timings.maxUs);
    Serial.printf("  readAt=%-4llu writeAt=%-4llu writeFile=%-4llu rename=%-4llu remove=%-4llu lock=%-4llu\n",
        r.fs.readAt, r.fs.writeAt, r.fs.writeFile, r.fs.renameFile, r.fs.removeFile, r.fs.lockAcquire);
    Serial.printf("  bytesW=%-10llu bytesR=%-10llu\n",
        r.fs.bytesWritten, r.fs.bytesRead);
    if (r.failed) {
        Serial.printf("  FAILED: %s\n", r.failReason);
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);

    // Initialize partitionLabel_ so format() calls work later.
    // formatOnFail=true handles a corrupted FS from a previous interrupted run.
    LittleFS.begin(true);

    Serial.println("=== pqueue on-device LittleFS profiler ===");
    Serial.printf("records per scenario: %u  body: %u B\n\n", kRecordsPerScenario, kRealisticBodyBytes);

    for (const uint32_t recordSize : kRecordSizes) {
        Serial.printf("--- record_size=%u B ---\n", recordSize);

        printResult(runQueueEnqueue(recordSize));
        printResult(runQueuePeekPop(recordSize));
        printResult(runHttpOfflineSubmit(recordSize));
        printResult(runHttpDrainBacklog(recordSize));

        Serial.println();
    }

    Serial.println("=== done ===");
}

void loop() {
    delay(60000);
}

#endif // ARDUINO
