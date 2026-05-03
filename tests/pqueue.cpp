#include "pqueue/queue.h"
#include "pqueue/storage_common.h"

#include "doctest/doctest.h"

#ifndef ARDUINO
#include <filesystem>
#include <fstream>
#endif

namespace {

#ifndef ARDUINO
const std::filesystem::path kSpoolDir = "pqueue_test_spool";

void cleanSpool() {
    std::error_code ec;
    std::filesystem::remove_all(kSpoolDir, ec);
}
#endif

} // namespace

TEST_CASE("pqueue starts empty") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    pqueue::Queue queue(config);

    std::string out;
    CHECK_FALSE(queue.peek(out).ok());
    CHECK_EQ(queue.stats().count, 0U);
#endif
}

TEST_CASE("pqueue preserves FIFO order") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    pqueue::Queue queue(config);

    REQUIRE(queue.enqueue("first").ok());
    REQUIRE(queue.enqueue("second").ok());

    std::string out;
    REQUIRE(queue.peek(out).ok());
    CHECK_EQ(out, "first");
    REQUIRE(queue.pop().ok());
    REQUIRE(queue.peek(out).ok());
    CHECK_EQ(out, "second");
    REQUIRE(queue.pop().ok());
    CHECK_FALSE(queue.peek(out).ok());
#endif
}

TEST_CASE("pqueue survives reopening from disk") {
#ifndef ARDUINO
    cleanSpool();
    {
        pqueue::Config config;
        config.basePath = kSpoolDir.string();
        pqueue::Queue queue(config);
        REQUIRE(queue.enqueue("one").ok());
        REQUIRE(queue.enqueue("two").ok());
    }

    pqueue::Config reopenedConfig;
    reopenedConfig.basePath = kSpoolDir.string();
    pqueue::Queue reopened(reopenedConfig);

    std::string out;
    REQUIRE(reopened.peek(out).ok());
    CHECK_EQ(out, "one");
    CHECK_EQ(reopened.stats().count, 2U);
#endif
}

TEST_CASE("pqueue rewriteFront updates the front record without popping it") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    pqueue::Queue queue(config);

    REQUIRE(queue.enqueue("retry=0").ok());
    REQUIRE(queue.rewriteFront("retry=1").ok());

    std::string out;
    REQUIRE(queue.peek(out).ok());
    CHECK_EQ(out, "retry=1");
#endif
}

TEST_CASE("pqueue rejects records over the configured max size") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.recordSizeBytes = 4;
    config.reservedBytes = 128;
    pqueue::Queue queue(config);

    CHECK_FALSE(queue.enqueue("12345").ok());
    CHECK_EQ(queue.stats().count, 0U);
#endif
}


TEST_CASE("pqueue rejects newest record when the fixed ring is full") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.recordSizeBytes = 8;
    config.reservedBytes = static_cast<std::uint32_t>((sizeof(pqueue::storage_detail::RecordHeader) + config.recordSizeBytes) * 2);
    pqueue::Queue queue(config);

    REQUIRE(queue.enqueue("one").ok());
    REQUIRE(queue.enqueue("two").ok());
    const auto full = queue.enqueue("three");

    CHECK_FALSE(full.ok());
    CHECK(full.code == pqueue::StatusCode::QueueFull);
    CHECK_EQ(queue.stats().count, 2U);

    std::string out;
    REQUIRE(queue.peek(out).ok());
    CHECK_EQ(out, "one");
#endif
}

TEST_CASE("pqueue lock prevents two active queues using the same spool") {
#ifndef ARDUINO
    cleanSpool();

    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.recordSizeBytes = 32;
    config.reservedBytes = 160;

    pqueue::Queue first(config);
    REQUIRE(first.enqueue("held").ok());

    pqueue::Queue second(config);
    const auto status = second.enqueue("blocked");
    CHECK_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::LockTimeout);

    std::string out;
    REQUIRE(first.peek(out).ok());
    CHECK_EQ(out, "held");
#endif
}


TEST_CASE("pqueue recovers stale POSIX pid lock") {
#ifndef ARDUINO
    cleanSpool();

    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.recordSizeBytes = 32;
    config.reservedBytes = 160;

    {
        pqueue::Queue queue(config);
        REQUIRE(queue.enqueue("before").ok());
    }

    std::filesystem::create_directories(kSpoolDir);
    std::ofstream lock(kSpoolDir / ".pqueue.lock", std::ios::binary | std::ios::trunc);
    lock << "pqueue-lock-v1\n";
    lock << "pid=99999999\n";
    lock << "token=stale-test\n";
    lock.close();

    pqueue::Queue reopened(config);
    REQUIRE(reopened.enqueue("after").ok());
    CHECK_EQ(reopened.stats().count, 2U);
#endif
}

TEST_CASE("pqueue releases lock when queue is destroyed") {
#ifndef ARDUINO
    cleanSpool();

    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.recordSizeBytes = 32;
    config.reservedBytes = 160;

    {
        pqueue::Queue first(config);
        REQUIRE(first.enqueue("first").ok());
    }

    pqueue::Queue second(config);
    REQUIRE(second.enqueue("second").ok());
    CHECK_EQ(second.stats().count, 2U);
#endif
}
