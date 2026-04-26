#include "api/disk_buffer.h"
#include "api/request_store.h"
#include "config.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

class FakeRequestStore final : public api::RequestStore {
public:
    api::RequestStoreIndex index;
    std::map<std::uint32_t, api::BufferedRequest> requests;

    bool readIndexOk = true;
    bool writeIndexOk = true;
    bool writeRequestOk = true;
    bool readRequestOk = true;
    bool removeRequestOk = true;
    std::uint64_t availableBytes = 1024 * 1024;

    int readIndexCalls = 0;
    int writeIndexCalls = 0;
    int writeRequestCalls = 0;
    int readRequestCalls = 0;
    int removeRequestCalls = 0;

    std::vector<std::uint32_t> writtenSequences;
    std::vector<std::uint32_t> readSequences;
    std::vector<std::uint32_t> removedSequences;

    bool readIndex(api::RequestStoreIndex& out) override {
        ++readIndexCalls;
        if (!readIndexOk) {
            return false;
        }
        out = index;
        return true;
    }

    bool writeIndex(const api::RequestStoreIndex& next) override {
        ++writeIndexCalls;
        if (!writeIndexOk) {
            return false;
        }
        index = next;
        return true;
    }

    bool writeRequest(std::uint32_t sequence, const api::BufferedRequest& request) override {
        ++writeRequestCalls;
        writtenSequences.push_back(sequence);
        if (!writeRequestOk) {
            return false;
        }
        requests[sequence] = request;
        return true;
    }

    bool readRequest(std::uint32_t sequence, api::BufferedRequest& out) override {
        ++readRequestCalls;
        readSequences.push_back(sequence);
        if (!readRequestOk) {
            return false;
        }
        const auto it = requests.find(sequence);
        if (it == requests.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool removeRequest(std::uint32_t sequence) override {
        ++removeRequestCalls;
        removedSequences.push_back(sequence);
        if (!removeRequestOk) {
            return false;
        }
        requests.erase(sequence);
        return true;
    }

    std::uint64_t freeBytes() override {
        return availableBytes;
    }
};

api::BufferedRequest bufferedReading(const std::string& name) {
    api::BufferedRequest out;
    out.path = "/" + name;
    out.mac = "mac-for-" + name;
    out.body = "{\"name\":\"" + name + "\"}";
    return out;
}

ApiBufferConfig bufferConfig() {
    ApiBufferConfig out;
    out.diskReserveBytes = 1024;
    return out;
}

class DiskBufferScenario {
public:
    static DiskBufferScenario emptyDiskBuffer() {
        return DiskBufferScenario();
    }

    static DiskBufferScenario withRequestsAlreadyOnDisk(std::vector<std::string> names) {
        DiskBufferScenario scenario;
        for (const auto& name : names) {
            const auto sequence = scenario.store_.index.tail;
            scenario.store_.requests[sequence] = bufferedReading(name);
            scenario.store_.index.tail += 1;
            scenario.store_.index.count += 1;
        }
        return scenario;
    }

    void loadFromDisk() {
        REQUIRE(api::disk_buffer::load(state_, store_));
    }

    void restartFromDisk() {
        state_ = api::disk_buffer::State{};
        loadFromDisk();
    }

    void enqueueReading(const std::string& name) {
        REQUIRE(api::disk_buffer::enqueue(state_, bufferedReading(name), bufferConfig(), store_));
    }

    bool tryToEnqueueReading(const std::string& name) {
        return api::disk_buffer::enqueue(state_, bufferedReading(name), bufferConfig(), store_);
    }

    void consumeConfirmedFront() {
        REQUIRE(api::disk_buffer::consume(state_, store_));
    }

    bool tryToConsumeConfirmedFront() {
        return api::disk_buffer::consume(state_, store_);
    }

    void dropFront() {
        REQUIRE(api::disk_buffer::dropFront(state_, store_));
    }

    void rewriteFrontRetryCounts(int timeoutRetries, int tlsRetries) {
        api::BufferedRequest front;
        REQUIRE(api::disk_buffer::peek(state_, front, store_));
        front.timeoutRetryCount = timeoutRetries;
        front.tlsRetryCount = tlsRetries;
        REQUIRE(api::disk_buffer::rewriteFront(state_, front, store_));
    }

    std::string frontReadingName() {
        api::BufferedRequest front;
        REQUIRE(api::disk_buffer::peek(state_, front, store_));
        return readingName(front);
    }

    std::vector<std::string> visibleReadingNamesByDrainingCopy() const {
        DiskBufferScenario copy = *this;
        std::vector<std::string> names;
        api::BufferedRequest front;
        while (api::disk_buffer::peek(copy.state_, front, copy.store_)) {
            names.push_back(readingName(front));
            if (!api::disk_buffer::consume(copy.state_, copy.store_)) {
                break;
            }
        }
        return names;
    }

    void makeDiskReserveAlreadyExhausted() {
        store_.availableBytes = bufferConfig().diskReserveBytes;
    }

    void makeRequestWriteFail() {
        store_.writeRequestOk = false;
    }

    void makeIndexWriteFail() {
        store_.writeIndexOk = false;
    }

    void makeRequestRemoveFail() {
        store_.removeRequestOk = false;
    }

    std::uint32_t queuedCount() const {
        return state_.count;
    }

    std::uint32_t headSequence() const {
        return state_.head;
    }

    std::uint32_t tailSequence() const {
        return state_.tail;
    }

    int readIndexCalls() const {
        return store_.readIndexCalls;
    }

    int writeRequestCalls() const {
        return store_.writeRequestCalls;
    }

    int writeIndexCalls() const {
        return store_.writeIndexCalls;
    }

    int removeRequestCalls() const {
        return store_.removeRequestCalls;
    }

    bool durableStoreHasNoRequestFiles() const {
        return store_.requests.empty();
    }

    std::vector<std::uint32_t> writtenSequences() const {
        return store_.writtenSequences;
    }

    std::vector<std::uint32_t> removedSequences() const {
        return store_.removedSequences;
    }

    api::BufferedRequest frontRequest() {
        api::BufferedRequest front;
        REQUIRE(api::disk_buffer::peek(state_, front, store_));
        return front;
    }

private:
    static std::string readingName(const api::BufferedRequest& request) {
        const std::string marker = "\"name\":\"";
        const auto start = request.body.find(marker);
        if (start == std::string::npos) {
            return "";
        }
        const auto valueStart = start + marker.size();
        const auto valueEnd = request.body.find('"', valueStart);
        if (valueEnd == std::string::npos) {
            return "";
        }
        return request.body.substr(valueStart, valueEnd - valueStart);
    }

    FakeRequestStore store_;
    api::disk_buffer::State state_;
};

void expectVisibleReadings(DiskBufferScenario scenario, std::vector<std::string> expectedNames) {
    CHECK_EQ(scenario.visibleReadingNamesByDrainingCopy(), expectedNames);
}

} // namespace

TEST_CASE("disk buffer loads queued readings once and reuses the loaded state") {
    auto buffer = DiskBufferScenario::withRequestsAlreadyOnDisk({"first reading", "second reading", "third reading"});

    buffer.loadFromDisk();

    CHECK_EQ(buffer.frontReadingName(), "first reading");
    CHECK_EQ(buffer.queuedCount(), 3U);
    CHECK_EQ(buffer.readIndexCalls(), 1);

    CHECK_EQ(buffer.frontReadingName(), "first reading");
    CHECK_EQ(buffer.readIndexCalls(), 1);
}

TEST_CASE("disk buffer appends a new reading after the existing disk backlog") {
    auto buffer = DiskBufferScenario::withRequestsAlreadyOnDisk({"older reading", "middle reading"});

    buffer.enqueueReading("newest reading");

    CHECK_EQ(buffer.writtenSequences(), std::vector<std::uint32_t>{2});
    CHECK_EQ(buffer.headSequence(), 0U);
    CHECK_EQ(buffer.tailSequence(), 3U);
    CHECK_EQ(buffer.queuedCount(), 3U);
    expectVisibleReadings(buffer, {"older reading", "middle reading", "newest reading"});
}

TEST_CASE("disk buffer refuses a new reading when the reserved filesystem space would be crossed") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.makeDiskReserveAlreadyExhausted();

    CHECK_FALSE(buffer.tryToEnqueueReading("reading while disk is reserved"));

    CHECK_EQ(buffer.writeRequestCalls(), 0);
    CHECK_EQ(buffer.writeIndexCalls(), 0);
    CHECK_EQ(buffer.queuedCount(), 0U);
}

TEST_CASE("disk buffer does not queue a reading when writing the request fails") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.makeRequestWriteFail();

    CHECK_FALSE(buffer.tryToEnqueueReading("reading that cannot be written"));

    CHECK_EQ(buffer.writeRequestCalls(), 1);
    CHECK_EQ(buffer.writeIndexCalls(), 0);
    CHECK_EQ(buffer.queuedCount(), 0U);
}

TEST_CASE("disk buffer failed enqueue must not leave a durable orphan request") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.makeIndexWriteFail();

    CHECK_FALSE(buffer.tryToEnqueueReading("reading whose index update fails"));

    CHECK_EQ(buffer.writeRequestCalls(), 1);
    CHECK_EQ(buffer.writeIndexCalls(), 1);
    CHECK_EQ(buffer.queuedCount(), 0U);
    CHECK(buffer.durableStoreHasNoRequestFiles());
}

TEST_CASE("disk buffer preserves queued reading order while consuming confirmed sends") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.enqueueReading("first reading");
    buffer.enqueueReading("second reading");
    buffer.enqueueReading("third reading");

    CHECK_EQ(buffer.frontReadingName(), "first reading");
    buffer.consumeConfirmedFront();

    CHECK_EQ(buffer.frontReadingName(), "second reading");
    buffer.consumeConfirmedFront();

    CHECK_EQ(buffer.frontReadingName(), "third reading");
    CHECK_EQ(buffer.removedSequences(), std::vector<std::uint32_t>{0, 1});
}

TEST_CASE("disk buffer drop front removes only the oldest queued reading") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.enqueueReading("oldest reading");
    buffer.enqueueReading("newer reading");

    buffer.dropFront();

    CHECK_EQ(buffer.frontReadingName(), "newer reading");
    CHECK_EQ(buffer.headSequence(), 1U);
    CHECK_EQ(buffer.queuedCount(), 1U);
}

TEST_CASE("disk buffer does not consume a confirmed send when the state update fails") {
    auto buffer = DiskBufferScenario::withRequestsAlreadyOnDisk({"reading still pending"});
    buffer.loadFromDisk();
    buffer.makeIndexWriteFail();

    CHECK_FALSE(buffer.tryToConsumeConfirmedFront());

    CHECK_EQ(buffer.headSequence(), 0U);
    CHECK_EQ(buffer.tailSequence(), 1U);
    CHECK_EQ(buffer.queuedCount(), 1U);
    CHECK_EQ(buffer.removeRequestCalls(), 0);
}

TEST_CASE("disk buffer failed consume must leave the request visible after restart") {
    auto buffer = DiskBufferScenario::withRequestsAlreadyOnDisk({"first reading", "second reading"});
    buffer.loadFromDisk();
    buffer.makeRequestRemoveFail();

    CHECK_FALSE(buffer.tryToConsumeConfirmedFront());

    CHECK_EQ(buffer.headSequence(), 0U);
    CHECK_EQ(buffer.queuedCount(), 2U);

    buffer.restartFromDisk();
    CHECK_EQ(buffer.frontReadingName(), "first reading");
}

TEST_CASE("disk buffer rewrite front updates retry metadata without changing order") {
    auto buffer = DiskBufferScenario::emptyDiskBuffer();
    buffer.enqueueReading("first reading");
    buffer.enqueueReading("second reading");

    buffer.rewriteFrontRetryCounts(2, 1);

    const auto front = buffer.frontRequest();
    CHECK_EQ(buffer.frontReadingName(), "first reading");
    CHECK_EQ(front.timeoutRetryCount, 2);
    CHECK_EQ(front.tlsRetryCount, 1);

    buffer.consumeConfirmedFront();
    CHECK_EQ(buffer.frontReadingName(), "second reading");
}
