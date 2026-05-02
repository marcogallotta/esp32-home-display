#include "pqueue/queue.h"

#include "doctest/doctest.h"

#ifndef ARDUINO
#include <filesystem>
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
    CHECK_FALSE(queue.peek(out));
    CHECK_EQ(queue.stats().count, 0U);
#endif
}

TEST_CASE("pqueue preserves FIFO order") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    pqueue::Queue queue(config);

    REQUIRE(queue.enqueue("first"));
    REQUIRE(queue.enqueue("second"));

    std::string out;
    REQUIRE(queue.peek(out));
    CHECK_EQ(out, "first");
    REQUIRE(queue.pop());
    REQUIRE(queue.peek(out));
    CHECK_EQ(out, "second");
    REQUIRE(queue.pop());
    CHECK_FALSE(queue.peek(out));
#endif
}

TEST_CASE("pqueue survives reopening from disk") {
#ifndef ARDUINO
    cleanSpool();
    {
        pqueue::Config config;
        config.basePath = kSpoolDir.string();
        pqueue::Queue queue(config);
        REQUIRE(queue.enqueue("one"));
        REQUIRE(queue.enqueue("two"));
    }

    pqueue::Config reopenedConfig;
    reopenedConfig.basePath = kSpoolDir.string();
    pqueue::Queue reopened(reopenedConfig);

    std::string out;
    REQUIRE(reopened.peek(out));
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

    REQUIRE(queue.enqueue("retry=0"));
    REQUIRE(queue.rewriteFront("retry=1"));

    std::string out;
    REQUIRE(queue.peek(out));
    CHECK_EQ(out, "retry=1");
#endif
}

TEST_CASE("pqueue rejects records over the configured max size") {
#ifndef ARDUINO
    cleanSpool();
    pqueue::Config config;
    config.basePath = kSpoolDir.string();
    config.maxRecordBytes = 4;
    config.diskReserveBytes = 0;
    pqueue::Queue queue(config);

    CHECK_FALSE(queue.enqueue("12345"));
    CHECK_EQ(queue.stats().count, 0U);
#endif
}
