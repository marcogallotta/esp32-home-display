#include "pqueue/outbox.h"

#include "doctest/doctest.h"

#ifndef ARDUINO
#include <filesystem>
#include <string>
#include <vector>
#endif

namespace {

#ifndef ARDUINO
const std::filesystem::path kOutboxSpoolDir = "pqueue_outbox_test_spool";

void cleanOutboxSpool() {
    std::error_code ec;
    std::filesystem::remove_all(kOutboxSpoolDir, ec);
}

struct FakeClock {
    std::uint64_t nowMs = 1000;
};

std::uint64_t fakeClockNow(void* context) {
    return static_cast<FakeClock*>(context)->nowMs;
}

struct FakeSender {
    std::vector<pqueue::SendDecision> decisions;
    std::vector<std::string> payloads;
    std::vector<pqueue::RetryState> retries;
};

pqueue::SendResult fakeSend(void* context, const std::string& payload, const pqueue::RetryState& retry) {
    auto* sender = static_cast<FakeSender*>(context);
    sender->payloads.push_back(payload);
    sender->retries.push_back(retry);

    if (sender->decisions.empty()) {
        return {pqueue::SendDecision::Sent};
    }

    const pqueue::SendDecision decision = sender->decisions.front();
    sender->decisions.erase(sender->decisions.begin());
    return {decision};
}

pqueue::OutboxConfig testOutboxConfig() {
    pqueue::OutboxConfig config;
    config.retryDelayMs = 1000;
    config.maxDrainAttemptsPerSecond = 1;
    return config;
}

pqueue::Outbox makeOutbox(
    FakeSender& sender,
    FakeClock& clock,
    pqueue::OutboxConfig outboxConfig
) {
    pqueue::Config queueConfig;
    queueConfig.basePath = kOutboxSpoolDir.string();
    return pqueue::Outbox(queueConfig, outboxConfig, fakeSend, &sender, fakeClockNow, &clock);
}

pqueue::Outbox makeOutbox(FakeSender& sender, FakeClock& clock) {
    return makeOutbox(sender, clock, testOutboxConfig());
}
#endif

} // namespace

TEST_CASE("pqueue outbox sends immediately when queue is empty") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    FakeClock clock;

    auto outbox = makeOutbox(sender, clock);
    const auto result = outbox.submit("fresh");

    CHECK(result.status == pqueue::SubmitStatus::Sent);
    CHECK_EQ(sender.payloads.size(), 1U);
    CHECK_EQ(sender.payloads[0], "fresh");
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}

TEST_CASE("pqueue outbox queues retryable fresh send failure") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;

    auto outbox = makeOutbox(sender, clock);
    const auto result = outbox.submit("fresh");

    CHECK(result.status == pqueue::SubmitStatus::Queued);
    CHECK_EQ(outbox.stats().count, 1U);
#endif
}

TEST_CASE("pqueue outbox preserves FIFO when backlog exists") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;

    auto outbox = makeOutbox(sender, clock);
    REQUIRE(outbox.submit("first").status == pqueue::SubmitStatus::Queued);
    CHECK(outbox.submit("second").status == pqueue::SubmitStatus::Queued);
    CHECK_EQ(sender.payloads.size(), 1U);

    clock.nowMs += 1000;
    sender.decisions.push_back(pqueue::SendDecision::Sent);
    auto drain = outbox.drain();

    CHECK_EQ(drain.sent, 1U);
    REQUIRE_EQ(sender.payloads.size(), 2U);
    CHECK_EQ(sender.payloads[1], "first");
    CHECK_EQ(outbox.stats().count, 1U);

    clock.nowMs += 1000;
    sender.decisions.push_back(pqueue::SendDecision::Sent);
    drain = outbox.drain();

    CHECK_EQ(drain.sent, 1U);
    REQUIRE_EQ(sender.payloads.size(), 3U);
    CHECK_EQ(sender.payloads[2], "second");
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}

TEST_CASE("pqueue outbox respects retry delay") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;

    auto outbox = makeOutbox(sender, clock);
    REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

    clock.nowMs += 500;
    auto drain = outbox.drain();

    CHECK(drain.notDue);
    CHECK_EQ(sender.payloads.size(), 1U);
    CHECK_EQ(outbox.stats().count, 1U);
#endif
}

TEST_CASE("pqueue outbox retries transient failures indefinitely") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;
    pqueue::OutboxConfig config;
    config.maxAttempts = 2;
    config.retryDelayMs = 1000;

    auto outbox = makeOutbox(sender, clock, config);
    REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

    clock.nowMs += 1000;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    auto drain = outbox.drain();

    CHECK_EQ(drain.droppedMaxAttempts, 0U);
    CHECK_EQ(outbox.stats().count, 1U);
#endif
}

TEST_CASE("pqueue outbox drops corrupt front records") {
#ifndef ARDUINO
    cleanOutboxSpool();
    {
        pqueue::Config queueConfig;
        queueConfig.basePath = kOutboxSpoolDir.string();
        pqueue::Queue queue(queueConfig);
        REQUIRE(queue.enqueue("not an outbox envelope").ok());
    }

    FakeSender sender;
    FakeClock clock;

    auto outbox = makeOutbox(sender, clock);
    auto drain = outbox.drain();

    CHECK_EQ(drain.corruptDropped, 1U);
    CHECK_EQ(outbox.stats().count, 0U);
    CHECK(sender.payloads.empty());
#endif
}

TEST_CASE("pqueue outbox keeps retry cooldown in RAM only") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;

    {
        auto outbox = makeOutbox(sender, clock);
        REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

        clock.nowMs += 500;
        auto drain = outbox.drain();
        CHECK(drain.notDue);
    }

    sender.decisions.push_back(pqueue::SendDecision::Sent);
    auto restarted = makeOutbox(sender, clock);
    auto drain = restarted.drain();

    CHECK_FALSE(drain.notDue);
    CHECK_EQ(drain.sent, 1U);
    CHECK_EQ(restarted.stats().count, 0U);
#endif
}

TEST_CASE("pqueue outbox passes persisted attempts to sender") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;
    pqueue::OutboxConfig config = testOutboxConfig();
    config.retryDelayMs = 0;

    auto outbox = makeOutbox(sender, clock, config);
    REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    auto drain = outbox.drain();
    CHECK_EQ(drain.attempts, 1U);

    clock.nowMs += 1000;
    sender.decisions.push_back(pqueue::SendDecision::Sent);
    drain = outbox.drain();
    CHECK_EQ(drain.sent, 1U);

    REQUIRE_GE(sender.retries.size(), 3U);
    CHECK_EQ(sender.retries[1].attempts, 1U);
    CHECK_EQ(sender.retries[2].attempts, 2U);
#endif
}

TEST_CASE("pqueue outbox throttles drain attempts") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;
    pqueue::OutboxConfig config;
    config.retryDelayMs = 0;
    config.maxDrainAttemptsPerSecond = 1;

    auto outbox = makeOutbox(sender, clock, config);
    REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    auto first = outbox.drain();
    CHECK_EQ(first.attempts, 1U);

    auto second = outbox.drain();
    CHECK(second.rateLimited);
#endif
}

TEST_CASE("pqueue outbox allows first drain immediately") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;
    clock.nowMs = 0;
    pqueue::OutboxConfig config;
    config.retryDelayMs = 0;
    config.maxDrainAttemptsPerSecond = 1;

    auto outbox = makeOutbox(sender, clock, config);
    REQUIRE(outbox.submit("fresh").status == pqueue::SubmitStatus::Queued);

    sender.decisions.push_back(pqueue::SendDecision::Sent);
    auto drain = outbox.drain();

    CHECK_FALSE(drain.rateLimited);
    CHECK_EQ(drain.sent, 1U);
#endif
}

TEST_CASE("pqueue outbox burst drain sends multiple queued records in one call") {
#ifndef ARDUINO
    cleanOutboxSpool();
    FakeSender sender;
    sender.decisions.push_back(pqueue::SendDecision::RetryLater);
    FakeClock clock;
    pqueue::OutboxConfig config;
    config.retryDelayMs = 0;
    config.maxDrainAttemptsPerSecond = 1;

    auto outbox = makeOutbox(sender, clock, config);
    REQUIRE(outbox.submit("one").status == pqueue::SubmitStatus::Queued);
    REQUIRE(outbox.submit("two").status == pqueue::SubmitStatus::Queued);
    REQUIRE(outbox.submit("three").status == pqueue::SubmitStatus::Queued);
    REQUIRE(outbox.submit("four").status == pqueue::SubmitStatus::Queued);
    CHECK_EQ(outbox.stats().count, 4U);

    const auto first = outbox.drainBurst(3);
    CHECK_EQ(first.attempts, 3U);
    CHECK_EQ(first.sent, 3U);
    CHECK_FALSE(first.rateLimited);
    CHECK_EQ(outbox.stats().count, 1U);

    const auto second = outbox.drainBurst(3);
    CHECK_EQ(second.sent, 1U);
    CHECK_EQ(outbox.stats().count, 0U);
#endif
}
