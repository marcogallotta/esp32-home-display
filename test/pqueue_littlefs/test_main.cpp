#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>

#include <cstdint>
#include <memory>
#include <string>

#include "pqueue/outbox.h"
#include "pqueue/queue.h"
#include "pqueue/status.h"
#include "pqueue/storage_common.h"
#include "pqueue/types.h"

namespace {

constexpr const char* kBasePath = "/pqueue_test";
constexpr const char* kSpoolPath = "/pqueue_test/pqueue.spool";

std::uint64_t g_nowMs = 0;

struct FakeSender {
    pqueue::SendDecision decision = pqueue::SendDecision::Sent;
    std::uint16_t calls = 0;
    std::string lastPayload;
    std::uint8_t lastAttempts = 0;
};

std::uint64_t fakeClock(void*) {
    return g_nowMs;
}

pqueue::SendResult fakeSend(void* context, const std::string& payload, const pqueue::RetryState& retry) {
    auto* sender = static_cast<FakeSender*>(context);
    sender->calls += 1;
    sender->lastPayload = payload;
    sender->lastAttempts = retry.attempts;
    return {sender->decision};
}

std::uint32_t slotSize(std::size_t recordSizeBytes) {
    return static_cast<std::uint32_t>(sizeof(pqueue::storage_detail::RecordHeader) + recordSizeBytes);
}

pqueue::Config queueConfig(std::size_t recordSizeBytes = 32, std::uint32_t capacityRecords = 8) {
    pqueue::Config config;
    config.basePath = kBasePath;
    config.storageBackend = pqueue::StorageBackend::LittleFS;
    config.recordSizeBytes = recordSizeBytes;
    config.reservedBytes = slotSize(recordSizeBytes) * capacityRecords;
    return config;
}

pqueue::OutboxConfig outboxConfig() {
    pqueue::OutboxConfig config;
    config.retryDelayMs = 0;
    config.maxDrainAttemptsPerSecond = 1000;
    return config;
}

void cleanLittleFs() {
    LittleFS.end();
    TEST_ASSERT_TRUE_MESSAGE(LittleFS.begin(true), "LittleFS mount failed");
    TEST_ASSERT_TRUE_MESSAGE(LittleFS.format(), "LittleFS format failed");
    LittleFS.end();
    g_nowMs = 0;
}

void assertQueueEmpty(pqueue::Queue& queue) {
    std::string out;
    const pqueue::Status status = queue.peek(out);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::StatusCode::QueueEmpty), static_cast<int>(status.code));
}

void test_basic_fifo() {
    cleanLittleFs();
    pqueue::Queue queue(queueConfig());

    TEST_ASSERT_TRUE(queue.enqueue("one").ok());
    TEST_ASSERT_TRUE(queue.enqueue("two").ok());
    TEST_ASSERT_TRUE(queue.enqueue("three").ok());

    std::string out;
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("one", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());

    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("two", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());

    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("three", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());

    assertQueueEmpty(queue);
}

void test_remount_persistence() {
    cleanLittleFs();
    {
        pqueue::Queue queue(queueConfig());
        TEST_ASSERT_TRUE(queue.enqueue("one").ok());
        TEST_ASSERT_TRUE(queue.enqueue("two").ok());
        TEST_ASSERT_TRUE(queue.enqueue("three").ok());
    }

    pqueue::Queue queue(queueConfig());
    std::string out;
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("one", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("two", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("three", out.c_str());
}

void test_pop_persistence() {
    cleanLittleFs();
    {
        pqueue::Queue queue(queueConfig());
        TEST_ASSERT_TRUE(queue.enqueue("one").ok());
        TEST_ASSERT_TRUE(queue.enqueue("two").ok());
        TEST_ASSERT_TRUE(queue.enqueue("three").ok());
        TEST_ASSERT_TRUE(queue.pop().ok());
        TEST_ASSERT_TRUE(queue.pop().ok());
    }

    pqueue::Queue queue(queueConfig());
    std::string out;
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("three", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());
    assertQueueEmpty(queue);
}

void test_rewrite_front_persistence() {
    cleanLittleFs();
    {
        pqueue::Queue queue(queueConfig());
        TEST_ASSERT_TRUE(queue.enqueue("old").ok());
        TEST_ASSERT_TRUE(queue.enqueue("tail").ok());
        TEST_ASSERT_TRUE(queue.rewriteFront("new").ok());
    }

    pqueue::Queue queue(queueConfig());
    std::string out;
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("new", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("tail", out.c_str());
}

void test_capacity_full_behavior() {
    cleanLittleFs();
    pqueue::Queue queue(queueConfig(16, 2));

    TEST_ASSERT_TRUE(queue.enqueue("one").ok());
    TEST_ASSERT_TRUE(queue.enqueue("two").ok());
    const pqueue::Status full = queue.enqueue("three");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::StatusCode::QueueFull), static_cast<int>(full.code));
    TEST_ASSERT_EQUAL_UINT32(2, queue.stats().count);

    std::string out;
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("one", out.c_str());
    TEST_ASSERT_TRUE(queue.pop().ok());
    TEST_ASSERT_TRUE(queue.peek(out).ok());
    TEST_ASSERT_EQUAL_STRING("two", out.c_str());
}

void test_record_size_boundary() {
    cleanLittleFs();
    pqueue::Queue queue(queueConfig(4, 2));

    TEST_ASSERT_TRUE(queue.enqueue("1234").ok());
    const pqueue::Status tooLarge = queue.enqueue("12345");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::StatusCode::RecordTooLarge), static_cast<int>(tooLarge.code));
    TEST_ASSERT_EQUAL_UINT32(1, queue.stats().count);
}

void test_lock_conflict() {
    cleanLittleFs();
    pqueue::Queue first(queueConfig());
    pqueue::Queue second(queueConfig());

    TEST_ASSERT_TRUE(first.enqueue("held").ok());
    std::string out;
    const pqueue::Status locked = second.peek(out);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::StatusCode::LockTimeout), static_cast<int>(locked.code));
}

void test_corrupt_active_record() {
    cleanLittleFs();
    {
        pqueue::Queue queue(queueConfig());
        TEST_ASSERT_TRUE(queue.enqueue("one").ok());
    }

    File spool = LittleFS.open(kSpoolPath, "r+");
    TEST_ASSERT_TRUE_MESSAGE(spool, "failed to open pqueue spool for corruption");
    TEST_ASSERT_TRUE(spool.seek(sizeof(pqueue::storage_detail::RecordHeader), SeekSet));
    TEST_ASSERT_EQUAL_UINT(1, spool.write(static_cast<uint8_t>(0xff)));
    spool.flush();
    spool.close();

    pqueue::Queue queue(queueConfig());
    std::string out;
    const pqueue::Status readStatus = queue.peek(out);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::StatusCode::CrcMismatch), static_cast<int>(readStatus.code));

    const pqueue::ValidationResult validation = queue.validate();
    TEST_ASSERT_FALSE(validation.ok);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1, validation.errors.size());
}

void test_outbox_backlog_persistence() {
    cleanLittleFs();
    FakeSender retrying;
    retrying.decision = pqueue::SendDecision::RetryLater;
    {
        pqueue::Outbox outbox(queueConfig(), outboxConfig(), fakeSend, &retrying, fakeClock, nullptr);
        const pqueue::SubmitResult submitted = outbox.submit("payload");
        TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::SubmitStatus::Queued), static_cast<int>(submitted.status));
        TEST_ASSERT_EQUAL_UINT16(1, retrying.calls);
        TEST_ASSERT_EQUAL_UINT32(1, outbox.stats().count);
    }

    FakeSender succeeding;
    succeeding.decision = pqueue::SendDecision::Sent;
    pqueue::Outbox outbox(queueConfig(), outboxConfig(), fakeSend, &succeeding, fakeClock, nullptr);
    TEST_ASSERT_EQUAL_UINT32(1, outbox.stats().count);
    const pqueue::DrainResult drained = outbox.drain();
    TEST_ASSERT_EQUAL_UINT16(1, drained.attempts);
    TEST_ASSERT_EQUAL_UINT16(1, drained.sent);
    TEST_ASSERT_EQUAL_UINT32(0, outbox.stats().count);
    TEST_ASSERT_EQUAL_STRING("payload", succeeding.lastPayload.c_str());
}

void test_retryable_failure_does_not_drop() {
    cleanLittleFs();
    FakeSender retrying;
    retrying.decision = pqueue::SendDecision::RetryLater;
    {
        pqueue::Outbox outbox(queueConfig(), outboxConfig(), fakeSend, &retrying, fakeClock, nullptr);
        const pqueue::SubmitResult submitted = outbox.submit("payload");
        TEST_ASSERT_EQUAL_INT(static_cast<int>(pqueue::SubmitStatus::Queued), static_cast<int>(submitted.status));
        TEST_ASSERT_EQUAL_UINT32(1, outbox.stats().count);

        for (int i = 0; i < 3; ++i) {
            const pqueue::DrainResult drained = outbox.drainBurst(1);
            TEST_ASSERT_EQUAL_UINT16(1, drained.attempts);
            TEST_ASSERT_EQUAL_UINT16(0, drained.sent);
            TEST_ASSERT_EQUAL_UINT16(0, drained.dropped);
            TEST_ASSERT_FALSE(drained.queueError);
            TEST_ASSERT_EQUAL_UINT32(1, outbox.stats().count);
        }
    }

    pqueue::Queue queue(queueConfig());
    TEST_ASSERT_EQUAL_UINT32(1, queue.stats().count);
}

} // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_basic_fifo);
    RUN_TEST(test_remount_persistence);
    RUN_TEST(test_pop_persistence);
    RUN_TEST(test_rewrite_front_persistence);
    RUN_TEST(test_capacity_full_behavior);
    RUN_TEST(test_record_size_boundary);
    RUN_TEST(test_lock_conflict);
    RUN_TEST(test_corrupt_active_record);
    RUN_TEST(test_outbox_backlog_persistence);
    RUN_TEST(test_retryable_failure_does_not_drop);
    UNITY_END();
}

void loop() {}
