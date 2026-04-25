#include "api/buffer.h"

#include "doctest/doctest.h"

#include <cstdio>
#include <deque>
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

network::HttpResponse transportFailure(network::TransportResult transport) {
    network::HttpResponse r;
    r.transport = transport;
    r.statusCode = 0;
    r.error = "scripted failure";
    return r;
}

void bufferOrFail(api::BufferState& buffer, const std::string& name, const ApiBufferConfig& c) {
    CHECK_EQ(api::bufferRequest(buffer, request(name), c), api::BufferInsertResult::Buffered);
}

void removeDroppedLogFile() {
    std::remove("dropped_requests.jsonl");
}

} // namespace

TEST_CASE("buffer starts empty") {
    api::BufferState buffer;

    CHECK(buffer.requests.empty());
    CHECK_EQ(buffer.nextDrainAllowedAtEpochS, 0);
}

TEST_CASE("buffer rejects newest request when full and keeps existing FIFO contents") {
    api::BufferState buffer;
    const ApiBufferConfig c = config(/*capacity=*/2);

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);

    const api::BufferInsertResult result = api::bufferRequest(buffer, request("third"), c);

    CHECK_EQ(result, api::BufferInsertResult::DroppedNewRequestBufferFull);
    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first");
    CHECK_EQ(buffer.requests[1].path, "/second");
}

TEST_CASE("buffer drains oldest requests first") {
    api::BufferState buffer;
    FakePoster poster;
    const ApiBufferConfig c = config();

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);
    bufferOrFail(buffer, "third", c);
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 100, c, poster);

    CHECK_EQ(result.attempted, 3);
    CHECK_EQ(result.sent, 3);
    CHECK_EQ(result.dropped, 0);
    CHECK(buffer.requests.empty());

    REQUIRE_EQ(poster.calls().size(), 3U);
    CHECK_EQ(poster.calls()[0].path, "/first");
    CHECK_EQ(poster.calls()[1].path, "/second");
    CHECK_EQ(poster.calls()[2].path, "/third");
}

TEST_CASE("drain cap limits how many requests are sent per tick") {
    api::BufferState buffer;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/2, /*drainTickS=*/60);

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);
    bufferOrFail(buffer, "third", c);
    poster.respondWith(httpOk());
    poster.respondWith(httpOk());

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 100, c, poster);

    CHECK_EQ(result.attempted, 2);
    CHECK_EQ(result.sent, 2);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().path, "/third");
    CHECK_EQ(buffer.nextDrainAllowedAtEpochS, 160);
}

TEST_CASE("buffer does not drain before the next allowed tick") {
    api::BufferState buffer;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/2, /*drainTickS=*/60);

    bufferOrFail(buffer, "first", c);
    buffer.nextDrainAllowedAtEpochS = 160;

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 159, c, poster);

    CHECK(result.notDueYet);
    CHECK_EQ(result.attempted, 0);
    CHECK_EQ(poster.calls().size(), 0U);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().path, "/first");
}

TEST_CASE("retryable transport failure keeps first request and blocks later requests") {
    api::BufferState buffer;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);
    poster.respondWith(transportFailure(network::TransportResult::NetworkError));

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 100, c, poster);

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
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);
    poster.respondWith(httpStatus(503));

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 100, c, poster);

    CHECK_EQ(result.attempted, 1);
    CHECK(result.blockedByRetryableFailure);

    REQUIRE_EQ(buffer.requests.size(), 2U);
    CHECK_EQ(buffer.requests[0].path, "/first");
    CHECK_EQ(buffer.requests[1].path, "/second");
}

TEST_CASE("permanent failure drops first request and continues draining") {
    removeDroppedLogFile();

    api::BufferState buffer;
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/10, /*drainTickS=*/60);

    bufferOrFail(buffer, "first", c);
    bufferOrFail(buffer, "second", c);
    poster.respondWith(httpStatus(400));
    poster.respondWith(httpOk());

    const api::BufferDrainResult result = api::maybeDrainBuffer(buffer, 100, c, poster);

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
    FakePoster poster;
    const ApiBufferConfig c = config(/*capacity=*/10, /*drainCap=*/1, /*drainTickS=*/0);

    bufferOrFail(buffer, "first", c);
    poster.respondWith(transportFailure(network::TransportResult::Timeout));
    poster.respondWith(transportFailure(network::TransportResult::Timeout));
    poster.respondWith(transportFailure(network::TransportResult::Timeout));

    const api::BufferDrainResult first = api::maybeDrainBuffer(buffer, 100, c, poster);
    CHECK_EQ(first.attempted, 1);
    CHECK(first.blockedByRetryableFailure);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().timeoutRetryCount, 1);

    const api::BufferDrainResult second = api::maybeDrainBuffer(buffer, 100, c, poster);
    CHECK_EQ(second.attempted, 1);
    CHECK(second.blockedByRetryableFailure);
    REQUIRE_EQ(buffer.requests.size(), 1U);
    CHECK_EQ(buffer.requests.front().timeoutRetryCount, 2);

    const api::BufferDrainResult third = api::maybeDrainBuffer(buffer, 100, c, poster);
    CHECK_EQ(third.attempted, 1);
    CHECK_EQ(third.dropped, 1);
    CHECK(buffer.requests.empty());

    removeDroppedLogFile();
}
