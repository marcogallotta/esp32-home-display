#include "pqueue/http/outbox.h"

#include "doctest/doctest.h"

#ifndef ARDUINO
#include <filesystem>
#include <string>
#include <vector>
#endif

namespace {

#ifndef ARDUINO
const std::filesystem::path kHttpOutboxSpoolDir = "pqueue_http_outbox_test_spool";

void cleanHttpOutboxSpool() {
    std::error_code ec;
    std::filesystem::remove_all(kHttpOutboxSpoolDir, ec);
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

struct FakeHttpTransport : pqueue::http::Transport {
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
            return {200, pqueue::http::TransportError::None};
        }

        const pqueue::http::Response response = responses.front();
        responses.erase(responses.begin());
        return response;
    }
};

pqueue::http::Response callbackPost(
    void* context,
    const char* url,
    const pqueue::http::Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
) {
    return static_cast<FakeHttpTransport*>(context)->post(url, headers, headerCount, body, bodySize);
}

pqueue::http::Outbox makeHttpOutbox(
    FakeHttpTransport& transport,
    FakeClock& clock,
    const pqueue::http::Header* headers = nullptr,
    std::size_t headerCount = 0
) {
    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = kHttpOutboxSpoolDir.string();
    httpConfig.outbox.retryDelayMs = 1000;
    httpConfig.outbox.maxDrainAttemptsPerSecond = 1;
    httpConfig.baseUrl = "https://example.test/api";
    httpConfig.headers = headers;
    httpConfig.headerCount = headerCount;

    return pqueue::http::Outbox(httpConfig, transport, fakeClockNow, &clock);
}


struct SeenResponse {
    std::string path;
    int statusCode = pqueue::http::kNoStatusCode;
    pqueue::http::TransportError error = pqueue::http::TransportError::Unknown;
    std::string body;
};

struct ResponseObserver {
    std::vector<SeenResponse> seen;
};

void onResponse(void* context, const pqueue::http::RequestEnvelope& request, const pqueue::http::Response& response) {
    auto* observer = static_cast<ResponseObserver*>(context);
    observer->seen.push_back({request.path, response.statusCode, response.error, response.body});
}

struct SeenDrop {
    bool hasRequest = false;
    std::string path;
    pqueue::http::DropReason reason = pqueue::http::DropReason::DecodeFailed;
    bool hasResponse = false;
    int statusCode = pqueue::http::kNoStatusCode;
};

struct DropObserver {
    std::vector<SeenDrop> seen;
};

void onDrop(
    void* context,
    const pqueue::http::RequestEnvelope* request,
    pqueue::http::DropReason reason,
    const pqueue::http::Response* response
) {
    auto* observer = static_cast<DropObserver*>(context);
    SeenDrop drop;
    drop.reason = reason;
    if (request != nullptr) {
        drop.hasRequest = true;
        drop.path = request->path;
    }
    if (response != nullptr) {
        drop.hasResponse = true;
        drop.statusCode = response->statusCode;
    }
    observer->seen.push_back(drop);
}

struct CustomClassifier {
    pqueue::SendDecision decision = pqueue::SendDecision::Drop;
    std::vector<int> statuses;
};

pqueue::SendDecision customClassify(void* context, const pqueue::http::Response& response) {
    auto* classifier = static_cast<CustomClassifier*>(context);
    classifier->statuses.push_back(response.statusCode);
    return classifier->decision;
}
#endif

} // namespace

TEST_CASE("pqueue http outbox posts immediately when queue is empty") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    FakeClock clock;
    pqueue::http::Header headers[] = {{"X-API-Key", "secret"}, {"Content-Type", "application/json"}};

    auto outbox = makeHttpOutbox(transport, clock, headers, 2);
    const auto result = outbox.submitPost("/switchbot/reading", "{\"ok\":true}");

    CHECK(result.status == pqueue::SubmitStatus::Sent);
    REQUIRE_EQ(transport.posts.size(), 1U);
    CHECK_EQ(transport.posts[0].url, "https://example.test/api/switchbot/reading");
    CHECK_EQ(transport.posts[0].body, "{\"ok\":true}");
    REQUIRE_EQ(transport.posts[0].headers.size(), 2U);
    CHECK_EQ(std::string{transport.posts[0].headers[0].name}, "X-API-Key");
    CHECK_EQ(std::string{transport.posts[0].headers[0].value}, "secret");
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}

TEST_CASE("pqueue http outbox queues retryable response and drains later") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({503, pqueue::http::TransportError::None});
    FakeClock clock;

    auto outbox = makeHttpOutbox(transport, clock);
    auto submit = outbox.submitPost("/xiaomi/reading", "body");
    CHECK(submit.status == pqueue::SubmitStatus::Queued);
    CHECK_EQ(outbox.stats().count, 1U);

    clock.nowMs += 1000;
    transport.responses.push_back({200, pqueue::http::TransportError::None});
    auto drain = outbox.drain();

    CHECK_EQ(drain.sent, 1U);
    CHECK_EQ(outbox.stats().count, 0U);
    REQUIRE_EQ(transport.posts.size(), 2U);
    CHECK_EQ(transport.posts[1].url, "https://example.test/api/xiaomi/reading");
#endif
}

TEST_CASE("pqueue http outbox drops permanent status") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None});
    FakeClock clock;

    auto outbox = makeHttpOutbox(transport, clock);
    auto submit = outbox.submitPost("/bad", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Dropped);
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}


TEST_CASE("pqueue http outbox notifies when a request is dropped by classification") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None});
    FakeClock clock;
    DropObserver observer;

    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = kHttpOutboxSpoolDir.string();
    httpConfig.outbox.retryDelayMs = 1000;
    httpConfig.baseUrl = "https://example.test";
    httpConfig.dropContext = &observer;
    httpConfig.onDrop = onDrop;

    pqueue::http::Outbox outbox(httpConfig, transport, fakeClockNow, &clock);
    const auto submit = outbox.submitPost("/bad", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Dropped);
    REQUIRE_EQ(observer.seen.size(), 1U);
    CHECK(observer.seen[0].reason == pqueue::http::DropReason::ClassifiedDrop);
    CHECK(observer.seen[0].hasRequest);
    CHECK_EQ(observer.seen[0].path, "/bad");
    CHECK(observer.seen[0].hasResponse);
    CHECK_EQ(observer.seen[0].statusCode, 422);
#endif
}

TEST_CASE("pqueue http outbox notifies when retries reach max attempts") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({503, pqueue::http::TransportError::None});
    FakeClock clock;
    DropObserver observer;

    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = kHttpOutboxSpoolDir.string();
    httpConfig.outbox.maxAttempts = 1;
    httpConfig.outbox.retryDelayMs = 1000;
    httpConfig.baseUrl = "https://example.test";
    httpConfig.dropContext = &observer;
    httpConfig.onDrop = onDrop;

    pqueue::http::Outbox outbox(httpConfig, transport, fakeClockNow, &clock);
    const auto submit = outbox.submitPost("/retry", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Dropped);
    REQUIRE_EQ(observer.seen.size(), 1U);
    CHECK(observer.seen[0].reason == pqueue::http::DropReason::MaxAttempts);
    CHECK(observer.seen[0].hasRequest);
    CHECK_EQ(observer.seen[0].path, "/retry");
    CHECK(observer.seen[0].hasResponse);
    CHECK_EQ(observer.seen[0].statusCode, 503);
#endif
}

TEST_CASE("pqueue http outbox allows classifier override") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None});
    FakeClock clock;
    CustomClassifier classifier;
    classifier.decision = pqueue::SendDecision::RetryLater;

    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = kHttpOutboxSpoolDir.string();
    httpConfig.outbox.retryDelayMs = 1000;
    httpConfig.baseUrl = "https://example.test";
    httpConfig.classify = customClassify;
    httpConfig.classifyContext = &classifier;

    pqueue::http::Outbox outbox(httpConfig, transport, fakeClockNow, &clock);
    auto submit = outbox.submitPost("/bad", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Queued);
    REQUIRE_EQ(classifier.statuses.size(), 1U);
    CHECK_EQ(classifier.statuses[0], 422);
#endif
}

TEST_CASE("pqueue http callback transport adapts a post callback") {
#ifndef ARDUINO
    FakeHttpTransport fake;
    fake.responses.push_back({204, pqueue::http::TransportError::None});
    pqueue::http::CallbackTransport transport(callbackPost, &fake);

    const auto response = transport.post(
        "https://example.test/api",
        nullptr,
        0,
        reinterpret_cast<const std::uint8_t*>("body"),
        4
    );

    CHECK_EQ(response.statusCode, 204);
    REQUIRE_EQ(fake.posts.size(), 1U);
    CHECK_EQ(fake.posts[0].url, "https://example.test/api");
    CHECK_EQ(fake.posts[0].body, "body");
#endif
}


TEST_CASE("pqueue http outbox calls response callback with request and response") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    FakeHttpTransport transport;
    transport.responses.push_back({201, pqueue::http::TransportError::None, "{\"status\":\"created\"}"});
    FakeClock clock;
    ResponseObserver observer;

    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = kHttpOutboxSpoolDir.string();
    httpConfig.outbox.retryDelayMs = 1000;
    httpConfig.baseUrl = "https://example.test";
    httpConfig.responseContext = &observer;
    httpConfig.onResponse = onResponse;

    pqueue::http::Outbox outbox(httpConfig, transport, fakeClockNow, &clock);
    const auto result = outbox.submitPost("/reading", "body");

    CHECK(result.status == pqueue::SubmitStatus::Sent);
    REQUIRE_EQ(observer.seen.size(), 1U);
    CHECK_EQ(observer.seen[0].path, "/reading");
    CHECK_EQ(observer.seen[0].statusCode, 201);
    CHECK(observer.seen[0].error == pqueue::http::TransportError::None);
    CHECK_EQ(observer.seen[0].body, "{\"status\":\"created\"}");
#endif
}

TEST_CASE("pqueue http default classifier handles representative outcomes") {
    CHECK(pqueue::http::defaultClassifyResponse({204, pqueue::http::TransportError::None}) == pqueue::SendDecision::Sent);
    CHECK(pqueue::http::defaultClassifyResponse({408, pqueue::http::TransportError::None}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({500, pqueue::http::TransportError::None}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({502, pqueue::http::TransportError::None}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({pqueue::http::kNoStatusCode, pqueue::http::TransportError::Timeout}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({404, pqueue::http::TransportError::None}) == pqueue::SendDecision::Drop);
    CHECK(pqueue::http::defaultClassifyResponse({507, pqueue::http::TransportError::None}) == pqueue::SendDecision::Drop);
}
