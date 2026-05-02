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

struct FakeHttpTransport {
    std::vector<pqueue::http::Response> responses;
    std::vector<PostedRequest> posts;
};

pqueue::http::Response fakePost(
    void* context,
    const char* url,
    const pqueue::http::Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
) {
    auto* transport = static_cast<FakeHttpTransport*>(context);

    PostedRequest request;
    request.url = url == nullptr ? std::string{} : std::string{url};
    for (std::size_t i = 0; i < headerCount; ++i) {
        request.headers.push_back(headers[i]);
    }
    request.body.assign(reinterpret_cast<const char*>(body), bodySize);
    transport->posts.push_back(request);

    if (transport->responses.empty()) {
        return {200, pqueue::http::TransportError::None};
    }

    const pqueue::http::Response response = transport->responses.front();
    transport->responses.erase(transport->responses.begin());
    return response;
}

pqueue::http::Outbox makeHttpOutbox(
    pqueue::Queue& queue,
    FakeHttpTransport& transport,
    FakeClock& clock,
    const pqueue::http::Header* headers = nullptr,
    std::size_t headerCount = 0
) {
    pqueue::OutboxConfig outboxConfig;
    outboxConfig.retryDelayMs = 1000;
    outboxConfig.maxDrainAttemptsPerSecond = 1;

    pqueue::http::Config httpConfig;
    httpConfig.baseUrl = "https://example.test/api";
    httpConfig.headers = headers;
    httpConfig.headerCount = headerCount;
    httpConfig.post = fakePost;
    httpConfig.postContext = &transport;

    return pqueue::http::Outbox(queue, outboxConfig, httpConfig, fakeClockNow, &clock);
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
    pqueue::FileStore store(kHttpOutboxSpoolDir.string());
    pqueue::Queue queue(store);
    FakeHttpTransport transport;
    FakeClock clock;
    pqueue::http::Header headers[] = {{"X-API-Key", "secret"}, {"Content-Type", "application/json"}};

    auto outbox = makeHttpOutbox(queue, transport, clock, headers, 2);
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
    pqueue::FileStore store(kHttpOutboxSpoolDir.string());
    pqueue::Queue queue(store);
    FakeHttpTransport transport;
    transport.responses.push_back({503, pqueue::http::TransportError::None});
    FakeClock clock;

    auto outbox = makeHttpOutbox(queue, transport, clock);
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
    pqueue::FileStore store(kHttpOutboxSpoolDir.string());
    pqueue::Queue queue(store);
    FakeHttpTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None});
    FakeClock clock;

    auto outbox = makeHttpOutbox(queue, transport, clock);
    auto submit = outbox.submitPost("/bad", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Dropped);
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}

TEST_CASE("pqueue http outbox allows classifier override") {
#ifndef ARDUINO
    cleanHttpOutboxSpool();
    pqueue::FileStore store(kHttpOutboxSpoolDir.string());
    pqueue::Queue queue(store);
    FakeHttpTransport transport;
    transport.responses.push_back({422, pqueue::http::TransportError::None});
    FakeClock clock;
    CustomClassifier classifier;
    classifier.decision = pqueue::SendDecision::RetryLater;

    pqueue::OutboxConfig outboxConfig;
    outboxConfig.retryDelayMs = 1000;

    pqueue::http::Config httpConfig;
    httpConfig.baseUrl = "https://example.test";
    httpConfig.post = fakePost;
    httpConfig.postContext = &transport;
    httpConfig.classify = customClassify;
    httpConfig.classifyContext = &classifier;

    pqueue::http::Outbox outbox(queue, outboxConfig, httpConfig, fakeClockNow, &clock);
    auto submit = outbox.submitPost("/bad", "body");

    CHECK(submit.status == pqueue::SubmitStatus::Queued);
    REQUIRE_EQ(classifier.statuses.size(), 1U);
    CHECK_EQ(classifier.statuses[0], 422);
#endif
}

TEST_CASE("pqueue http default classifier retries unknown and transport errors") {
    CHECK(pqueue::http::defaultClassifyResponse({0, pqueue::http::TransportError::Unknown}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({0, pqueue::http::TransportError::Timeout}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({429, pqueue::http::TransportError::None}) == pqueue::SendDecision::RetryLater);
    CHECK(pqueue::http::defaultClassifyResponse({404, pqueue::http::TransportError::None}) == pqueue::SendDecision::Drop);
    CHECK(pqueue::http::defaultClassifyResponse({204, pqueue::http::TransportError::None}) == pqueue::SendDecision::Sent);
}
