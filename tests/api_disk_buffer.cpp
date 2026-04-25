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

api::BufferedRequest request(std::string name) {
    api::BufferedRequest out;
    out.path = "/" + name;
    out.mac = "AA:BB:CC:DD:EE:" + name;
    out.body = "{\"name\":\"" + name + "\"}";
    return out;
}

ApiBufferConfig config() {
    ApiBufferConfig out;
    out.diskReserveBytes = 1024;
    return out;
}

void expectRequestNamed(const api::BufferedRequest& actual, const char* name) {
    CHECK_EQ(actual.path, std::string("/") + name);
    CHECK_EQ(actual.mac, std::string("AA:BB:CC:DD:EE:") + name);
    CHECK_EQ(actual.body, std::string("{\"name\":\"") + name + "\"}");
}

} // namespace

TEST_CASE("disk buffer loads existing index once") {
    FakeRequestStore store;
    store.index.head = 4;
    store.index.tail = 7;
    store.index.count = 3;
    store.requests[4] = request("first");

    api::disk_buffer::State state;

    api::BufferedRequest front;
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "first");

    CHECK_EQ(state.head, 4U);
    CHECK_EQ(state.tail, 7U);
    CHECK_EQ(state.count, 3U);
    CHECK(state.loaded);
    CHECK_EQ(store.readIndexCalls, 1);

    REQUIRE(api::disk_buffer::peek(state, front, store));
    CHECK_EQ(store.readIndexCalls, 1);
}

TEST_CASE("disk buffer enqueue appends at tail and advances index") {
    FakeRequestStore store;
    store.index.head = 10;
    store.index.tail = 12;
    store.index.count = 2;

    api::disk_buffer::State state;

    REQUIRE(api::disk_buffer::enqueue(state, request("third"), config(), store));

    CHECK_EQ(store.writtenSequences.size(), 1U);
    CHECK_EQ(store.writtenSequences[0], 12U);
    CHECK_EQ(store.index.head, 10U);
    CHECK_EQ(store.index.tail, 13U);
    CHECK_EQ(store.index.count, 3U);
    CHECK_EQ(state.head, 10U);
    CHECK_EQ(state.tail, 13U);
    CHECK_EQ(state.count, 3U);
}

TEST_CASE("disk buffer refuses enqueue when disk reserve would be crossed") {
    FakeRequestStore store;
    store.availableBytes = config().diskReserveBytes;

    api::disk_buffer::State state;

    CHECK_FALSE(api::disk_buffer::enqueue(state, request("first"), config(), store));

    CHECK_EQ(store.writeRequestCalls, 0);
    CHECK_EQ(store.writeIndexCalls, 0);
    CHECK_EQ(state.count, 0U);
}

TEST_CASE("disk buffer does not advance when request write fails") {
    FakeRequestStore store;
    store.writeRequestOk = false;

    api::disk_buffer::State state;

    CHECK_FALSE(api::disk_buffer::enqueue(state, request("first"), config(), store));

    CHECK_EQ(store.writeRequestCalls, 1);
    CHECK_EQ(store.writeIndexCalls, 0);
    CHECK_EQ(state.head, 0U);
    CHECK_EQ(state.tail, 0U);
    CHECK_EQ(state.count, 0U);
}

TEST_CASE("disk buffer does not advance when enqueue index write fails") {
    FakeRequestStore store;
    store.writeIndexOk = false;

    api::disk_buffer::State state;

    CHECK_FALSE(api::disk_buffer::enqueue(state, request("first"), config(), store));

    CHECK_EQ(store.writeRequestCalls, 1);
    CHECK_EQ(store.writeIndexCalls, 1);
    CHECK_EQ(state.head, 0U);
    CHECK_EQ(state.tail, 0U);
    CHECK_EQ(state.count, 0U);
}

TEST_CASE("disk buffer preserves FIFO order across consume") {
    FakeRequestStore store;
    api::disk_buffer::State state;

    REQUIRE(api::disk_buffer::enqueue(state, request("first"), config(), store));
    REQUIRE(api::disk_buffer::enqueue(state, request("second"), config(), store));
    REQUIRE(api::disk_buffer::enqueue(state, request("third"), config(), store));

    api::BufferedRequest front;
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "first");

    REQUIRE(api::disk_buffer::consume(state, store));
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "second");

    REQUIRE(api::disk_buffer::consume(state, store));
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "third");

    CHECK_EQ(store.removedSequences.size(), 2U);
    CHECK_EQ(store.removedSequences[0], 0U);
    CHECK_EQ(store.removedSequences[1], 1U);
}

TEST_CASE("disk buffer drop front removes only the oldest request") {
    FakeRequestStore store;
    api::disk_buffer::State state;

    REQUIRE(api::disk_buffer::enqueue(state, request("first"), config(), store));
    REQUIRE(api::disk_buffer::enqueue(state, request("second"), config(), store));

    REQUIRE(api::disk_buffer::dropFront(state, store));

    api::BufferedRequest front;
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "second");
    CHECK_EQ(state.head, 1U);
    CHECK_EQ(state.count, 1U);
}

TEST_CASE("disk buffer does not advance when consume index write fails") {
    FakeRequestStore store;
    store.index.count = 1;
    store.index.tail = 1;
    store.requests[0] = request("first");

    api::disk_buffer::State state;
    REQUIRE(api::disk_buffer::load(state, store));

    store.writeIndexOk = false;
    CHECK_FALSE(api::disk_buffer::consume(state, store));

    CHECK_EQ(state.head, 0U);
    CHECK_EQ(state.tail, 1U);
    CHECK_EQ(state.count, 1U);
    CHECK_EQ(store.removeRequestCalls, 0);
}

TEST_CASE("disk buffer advances even if old request delete fails after index update") {
    FakeRequestStore store;
    store.index.count = 2;
    store.index.tail = 2;
    store.requests[0] = request("first");
    store.requests[1] = request("second");

    api::disk_buffer::State state;
    REQUIRE(api::disk_buffer::load(state, store));

    store.removeRequestOk = false;
    REQUIRE(api::disk_buffer::consume(state, store));

    CHECK_EQ(state.head, 1U);
    CHECK_EQ(state.count, 1U);

    api::BufferedRequest front;
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "second");
}

TEST_CASE("disk buffer rewrite front updates retry metadata without changing order") {
    FakeRequestStore store;
    api::disk_buffer::State state;

    REQUIRE(api::disk_buffer::enqueue(state, request("first"), config(), store));
    REQUIRE(api::disk_buffer::enqueue(state, request("second"), config(), store));

    api::BufferedRequest retried = request("first");
    retried.timeoutRetryCount = 2;
    retried.tlsRetryCount = 1;

    REQUIRE(api::disk_buffer::rewriteFront(state, retried, store));

    api::BufferedRequest front;
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "first");
    CHECK_EQ(front.timeoutRetryCount, 2);
    CHECK_EQ(front.tlsRetryCount, 1);

    REQUIRE(api::disk_buffer::consume(state, store));
    REQUIRE(api::disk_buffer::peek(state, front, store));
    expectRequestNamed(front, "second");
}
