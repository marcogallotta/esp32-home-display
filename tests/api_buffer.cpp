#include "api/buffer.h"
#include "api/request_store.h"

#include "doctest/doctest.h"

#include <cstdio>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

struct PostedRequest {
    std::string path;
    std::string body;
};

class FakePoster : public api::ApiPoster {
public:
    void respondWith(network::HttpResponse response) {
        responses_.push_back(std::move(response));
    }

    network::HttpResponse postJson(const std::string& path, const std::string& body) const override {
        calls_.push_back({path, body});

        if (responses_.empty()) {
            return response(network::TransportResult::InternalError, 0, "no scripted response");
        }

        network::HttpResponse next = responses_.front();
        responses_.pop_front();
        return next;
    }

    const std::vector<PostedRequest>& calls() const {
        return calls_;
    }

private:
    static network::HttpResponse response(
        network::TransportResult transport,
        int statusCode,
        const std::string& error = ""
    ) {
        network::HttpResponse r;
        r.transport = transport;
        r.statusCode = statusCode;
        r.error = error;
        return r;
    }

    mutable std::deque<network::HttpResponse> responses_;
    mutable std::vector<PostedRequest> calls_;
};

class FakeRequestStore final : public api::RequestStore {
public:
    api::RequestStoreIndex index;
    std::map<std::uint32_t, api::BufferedRequest> requests;

    bool readIndexOk = true;
    bool writeIndexOk = true;
    bool writeRequestOk = true;
    bool removeRequestOk = true;
    std::uint64_t availableBytes = 1024 * 1024;

    bool readIndex(api::RequestStoreIndex& out) override {
        if (!readIndexOk) {
            return false;
        }
        out = index;
        return true;
    }

    bool writeIndex(const api::RequestStoreIndex& next) override {
        if (!writeIndexOk) {
            return false;
        }
        index = next;
        return true;
    }

    bool writeRequest(std::uint32_t sequence, const api::BufferedRequest& request) override {
        if (!writeRequestOk) {
            return false;
        }
        requests[sequence] = request;
        return true;
    }

    bool readRequest(std::uint32_t sequence, api::BufferedRequest& out) override {
        const auto it = requests.find(sequence);
        if (it == requests.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool removeRequest(std::uint32_t sequence) override {
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

api::BufferedRequest request(const std::string& name) {
    api::BufferedRequest r;
    r.path = "/" + name;
    r.mac = "AA:BB:CC:DD:EE:" + name;
    r.body = "{\"name\":\"" + name + "\"}";
    return r;
}

ApiBufferConfig config(int capacity = 10, int drainCap = 10, int drainTickS = 60) {
    ApiBufferConfig c;
    c.inMemory = capacity;
    c.drainRateCap = drainCap;
    c.drainRateTickS = drainTickS;
    c.diskReserveBytes = 0;
    return c;
}

network::HttpResponse httpOk() {
    network::HttpResponse r;
    r.transport = network::TransportResult::Ok;
    r.statusCode = 200;
    return r;
}

network::HttpResponse httpStatus(int statusCode) {
    network::HttpResponse r;
    r.transport = network::TransportResult::Ok;
    r.statusCode = statusCode;
    return r;
}

std::uint64_t ms(int seconds) {
    return static_cast<std::uint64_t>(seconds) * 1000;
}

network::HttpResponse transportFailure(network::TransportResult transport) {
    network::HttpResponse r;
    r.transport = transport;
    r.statusCode = 0;
    r.error = "scripted failure";
    return r;
}

void bufferOrFail(
    api::BufferState& buffer,
    FakeRequestStore& store,
    const std::string& name,
    const ApiBufferConfig& c
) {
    CHECK_EQ(
        api::bufferRequest(buffer, request(name), c, store, 0ULL),
        api::BufferInsertResult::Buffered
    );
}

void removeDroppedLogFile() {
    std::remove("dropped_requests.jsonl");
}

} // namespace

TEST_CASE("buffer starts empty") {
    api::BufferState buffer;

    CHECK(buffer.requests.empty());
    CHECK_EQ(buffer.nextDrainAllowedAtMs, 0ULL);
    CHECK_EQ(buffer.disk.count, 0U);
}

TEST_CASE("buffering delays first drain but does not move an existing future drain") {
    api::BufferState buffer;
    FakeRequestStore store;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/2, /*drainTickS=*/60);

    CHECK_EQ(
        api::bufferRequest(buffer, request("first"), c, store, ms(10)),
        api::BufferInsertResult::Buffered
    );
    CHECK_EQ(buffer.nextDrainAllowedAtMs, ms(70));

    CHECK_EQ(
        api::bufferRequest(buffer, request("second"), c, store, ms(20)),
        api::BufferInsertResult::Buffered
    );
    CHECK_EQ(buffer.nextDrainAllowedAtMs, ms(70));
}

TEST_CASE("buffer spills newest request to disk when RAM is full and keeps RAM FIFO contents") {
    api::BufferState buffer;
    FakeRequestStore store;
    const ApiBufferConfig c = config(/*capacity=*/2);

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);

    const api::BufferInsertResult result =
        api::bufferRequest(buffer, request("third"), c, store, 0ULL);

    CHECK_EQ(result, api::BufferInsertResult::Buffered);
    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first");
    CHECK_EQ(buffer.requests[1].path, "/second");

    CHECK_EQ(buffer.disk.count, 1U);
    REQUIRE_EQ(store.requests.size(), 1U);
    CHECK_EQ(store.requests[0].path, "/third");
}

TEST_CASE("buffer fills RAM before spilling to disk even when disk backlog exists") {
    api::BufferState buffer;
    FakeRequestStore store;
    store.index.tail = 1;
    store.index.count = 1;
    store.requests[0] = request("existing");

    const ApiBufferConfig c = config(/*capacity=*/2);

    const api::BufferInsertResult first =
        api::bufferRequest(buffer, request("first-ram"), c, store, 0ULL);
    const api::BufferInsertResult second =
        api::bufferRequest(buffer, request("second-ram"), c, store, 0ULL);
    const api::BufferInsertResult third =
        api::bufferRequest(buffer, request("third-disk"), c, store, 0ULL);

    CHECK_EQ(first, api::BufferInsertResult::Buffered);
    CHECK_EQ(second, api::BufferInsertResult::Buffered);
    CHECK_EQ(third, api::BufferInsertResult::Buffered);

    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first-ram");
    CHECK_EQ(buffer.requests[1].path, "/second-ram");

    CHECK_EQ(buffer.disk.count, 2U);
    CHECK_EQ(store.requests[0].path, "/existing");
    CHECK_EQ(store.requests[1].path, "/third-disk");
}

TEST_CASE("buffer drops newest request when RAM is full and disk enqueue fails") {
    api::BufferState buffer;
    FakeRequestStore store;
    store.writeRequestOk = false;
    const ApiBufferConfig c = config(/*capacity=*/1);

    bufferOrFail(buffer, store, "first", c);

    const api::BufferInsertResult result =
        api::bufferRequest(buffer, request("second"), c, store, 0ULL);

    CHECK_EQ(result, api::BufferInsertResult::DroppedNewRequestBufferFull);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().path, "/first");
    CHECK_EQ(buffer.disk.count, 0U);
}

TEST_CASE("buffer drains oldest requests first") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config();

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);
    bufferOrFail(buffer, store, "third", c);
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);

    CHECK_EQ(result.attempted, 3);
    CHECK_EQ(result.sent, 3);
    CHECK_EQ(result.dropped, 0);
    CHECK(buffer.requests.empty());

    REQUIRE_EQ(poster.calls().size(), 3U);
    CHECK_EQ(poster.calls()[0].path, "/first");
    CHECK_EQ(poster.calls()[1].path, "/second");
    CHECK_EQ(poster.calls()[2].path, "/third");
}

TEST_CASE("startup drain sends persisted disk backlog immediately") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/2, /*drainCap=*/10, /*drainTickS=*/60);

    store.index.head = 0;
    store.index.tail = 2;
    store.index.count = 2;
    store.requests[0] = request("persisted-first");
    store.requests[1] = request("persisted-second");

    poster.respondWith(httpOk());
    poster.respondWith(httpOk());

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, 0ULL, c, poster, store);

    CHECK_EQ(result.attempted, 2);
    CHECK_EQ(result.sent, 2);
    CHECK_EQ(result.dropped, 0);
    CHECK_FALSE(result.notDueYet);
    CHECK_FALSE(result.blockedByRetryableFailure);

    CHECK(buffer.requests.empty());
    CHECK_EQ(buffer.disk.count, 0U);
    CHECK_EQ(store.index.count, 0U);
    CHECK(store.requests.empty());

    REQUIRE_EQ(poster.calls().size(), 2U);
    CHECK_EQ(poster.calls()[0].path, "/persisted-first");
    CHECK_EQ(poster.calls()[1].path, "/persisted-second");
}

TEST_CASE("retryable failure during startup drain preserves persisted disk backlog") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/2, /*drainCap=*/10, /*drainTickS=*/60);

    store.index.head = 0;
    store.index.tail = 2;
    store.index.count = 2;
    store.requests[0] = request("persisted-first");
    store.requests[1] = request("persisted-second");

    poster.respondWith(transportFailure(network::TransportResult::NetworkError));

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, 0ULL, c, poster, store);

    CHECK_EQ(result.attempted, 1);
    CHECK_EQ(result.sent, 0);
    CHECK_EQ(result.dropped, 0);
    CHECK(result.blockedByRetryableFailure);

    CHECK(buffer.requests.empty());
    CHECK_EQ(buffer.disk.count, 2U);
    CHECK_EQ(store.index.count, 2U);
    CHECK_EQ(store.requests[0].path, "/persisted-first");
    CHECK_EQ(store.requests[1].path, "/persisted-second");

    REQUIRE_EQ(poster.calls().size(), 1U);
    CHECK_EQ(poster.calls()[0].path, "/persisted-first");
}

TEST_CASE("drain cap limits how many requests are sent per tick") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/2, /*drainTickS=*/60);

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);
    bufferOrFail(buffer, store, "third", c);
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);

    CHECK_EQ(result.attempted, 2);
    CHECK_EQ(result.sent, 2);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().path, "/third");
    CHECK_EQ(buffer.nextDrainAllowedAtMs, ms(160));
}

TEST_CASE("buffer does not drain before the next allowed tick") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/2, /*drainTickS=*/60);

    bufferOrFail(buffer, store, "first", c);
    buffer.nextDrainAllowedAtMs = ms(160);

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(159), c, poster, store);

    CHECK(result.notDueYet);
    CHECK_EQ(result.attempted, 0);
    CHECK_EQ(poster.calls().size(), 0U);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().path, "/first");
}

TEST_CASE("retryable transport failure keeps first request and blocks later requests") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);
    poster.respondWith(transportFailure(network::TransportResult::NetworkError));

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);

    CHECK_EQ(result.attempted, 1);
    CHECK_EQ(result.sent, 0);
    CHECK_EQ(result.dropped, 0);
    CHECK(result.blockedByRetryableFailure);

    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first");
    CHECK_EQ(buffer.requests[1].path, "/second");

    REQUIRE_EQ(poster.calls().size(), 1U);
    CHECK_EQ(poster.calls()[0].path, "/first");
}

TEST_CASE("retryable HTTP failure keeps first request and blocks later requests") {
    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);
    poster.respondWith(httpStatus(503));

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);

    CHECK_EQ(result.attempted, 1);
    CHECK(result.blockedByRetryableFailure);

    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first");
    CHECK_EQ(buffer.requests[1].path, "/second");
}

TEST_CASE("permanent failure drops first request and continues draining") {
    removeDroppedLogFile();

    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, store, "first", c);
    bufferOrFail(buffer, store, "second", c);
    poster.respondWith(httpStatus(400));
    poster.respondWith(httpOk());

    const api::BufferDrainResult result =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);

    CHECK_EQ(result.attempted, 2);
    CHECK_EQ(result.dropped, 1);
    CHECK_EQ(result.sent, 1);
    CHECK_FALSE(result.blockedByRetryableFailure);
    CHECK(buffer.requests.empty());

    REQUIRE_EQ(poster.calls().size(), 2U);
    CHECK_EQ(poster.calls()[0].path, "/first");
    CHECK_EQ(poster.calls()[1].path, "/second");

    removeDroppedLogFile();
}

TEST_CASE("timeout failure is retried twice and then dropped") {
    removeDroppedLogFile();

    api::BufferState buffer;
    FakeRequestStore store;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/1, /*drainTickS=*/0);

    bufferOrFail(buffer, store, "first", c);
    poster.respondWith(transportFailure(network::TransportResult::Timeout));
    poster.respondWith(transportFailure(network::TransportResult::Timeout));
    poster.respondWith(transportFailure(network::TransportResult::Timeout));

    const api::BufferDrainResult first =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);
    CHECK_EQ(first.attempted, 1);
    CHECK(first.blockedByRetryableFailure);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().timeoutRetryCount, 1);

    const api::BufferDrainResult second =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);
    CHECK_EQ(second.attempted, 1);
    CHECK(second.blockedByRetryableFailure);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().timeoutRetryCount, 2);

    const api::BufferDrainResult third =
        api::maybeDrainBuffer(buffer, ms(100), c, poster, store);
    CHECK_EQ(third.attempted, 1);
    CHECK_EQ(third.dropped, 1);
    CHECK(buffer.requests.empty());

    removeDroppedLogFile();
}
