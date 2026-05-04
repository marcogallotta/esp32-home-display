#include "support/pqueue_file_store_support.h"

#include "doctest/doctest.h"

#ifndef ARDUINO

using pqueue_test::CapturedEvent;
using pqueue_test::captureEvent;
using pqueue_test::makeFakeFileSystem;
using pqueue_test::makeStore;
using pqueue_test::slotSize;

TEST_CASE("FileStore mounts and preallocates one spool file") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.mount().ok());
    CHECK_EQ(fileSystem->mountedBasePath, "/fake-pqueue");
    REQUIRE(fileSystem->exists("pqueue.spool"));
    CHECK_EQ(fileSystem->files["pqueue.spool"].size(), 3U * slotSize());
}

TEST_CASE("FileStore write/read uses fixed ring slots") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "zero").ok());
    REQUIRE(store.writeRecord(3, "three").ok());

    std::string out;
    REQUIRE(store.readRecord(3, out).ok());
    CHECK_EQ(out, "three");
    CHECK_FALSE(store.readRecord(0, out).ok());
}

TEST_CASE("FileStore rejects records larger than the configured slot payload") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, 96, 8);

    CHECK_FALSE(store.writeRecord(0, "123456789").ok());
}

TEST_CASE("FileStore emits diagnostic event when slot write fails") {
    auto fileSystem = makeFakeFileSystem();
    std::vector<CapturedEvent> events;
    auto store = makeStore(fileSystem, pqueue::EventOptions{captureEvent, &events});

    REQUIRE(store.mount().ok());
    fileSystem->failNextWrite();

    const auto status = store.writeRecord(0, "payload");
    REQUIRE_FALSE(status.ok());
    REQUIRE_EQ(events.size(), 1U);
    CHECK(events[0].kind == pqueue::EventKind::Diagnostic);
    CHECK(events[0].severity == pqueue::Severity::Error);
    CHECK(events[0].code == pqueue::StatusCode::WriteFailed);
}

TEST_CASE("FileStore rejects corrupt spool slot") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload").ok());
    fileSystem->corruptSlotHeader(0, slotSize());

    std::string out;
    CHECK_FALSE(store.readRecord(0, out).ok());
}

TEST_CASE("FileStore keeps the older valid metadata copy when the latest is corrupt") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeIndex({0, 1, 1}).ok());
    REQUIRE(store.writeIndex({0, 2, 2}).ok());
    REQUIRE(fileSystem->exists("pqueue.meta_a"));
    REQUIRE(fileSystem->exists("pqueue.meta_b"));

    fileSystem->corruptFile("pqueue.meta_a");

    pqueue::FileStoreIndex out;
    REQUIRE(store.readIndex(out).ok());
    CHECK_EQ(out.head, 0U);
    CHECK_EQ(out.tail, 1U);
    CHECK_EQ(out.count, 1U);
}

TEST_CASE("FileStore starts with an empty index when no metadata exists") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    pqueue::FileStoreIndex out;
    REQUIRE(store.readIndex(out).ok());
    CHECK_EQ(out.head, 0U);
    CHECK_EQ(out.tail, 0U);
    CHECK_EQ(out.count, 0U);
}

TEST_CASE("FileStore fails loudly when storage config changes") {
    auto fileSystem = makeFakeFileSystem();
    {
        auto store = makeStore(fileSystem, {}, 160, 32);
        REQUIRE(store.writeIndex({0, 1, 1}).ok());
    }

    auto reopenedWithDifferentSlotSize = makeStore(fileSystem, {}, 160, 64);
    pqueue::FileStoreIndex out;
    const auto status = reopenedWithDifferentSlotSize.readIndex(out);
    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::InvalidIndex);
}

TEST_CASE("FileStore rounds reserved bytes down to whole slots") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, static_cast<std::uint32_t>(slotSize() * 2 + 7), 32);

    REQUIRE(store.mount().ok());
    CHECK_EQ(fileSystem->files["pqueue.spool"].size(), 2U * slotSize());
}

TEST_CASE("FileStore rejects configs that cannot fit one slot") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, 4, 32);

    CHECK_FALSE(store.mount().ok());
}

TEST_CASE("FileStore removeRecord invalidates the slot") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload").ok());
    REQUIRE(store.removeRecord(0).ok());

    std::string out;
    CHECK_FALSE(store.readRecord(0, out).ok());
}


TEST_CASE("FileStore fails loudly when spool exists without metadata") {
    auto fileSystem = makeFakeFileSystem();
    fileSystem->files["pqueue.spool"] = std::string(slotSize() * 2, '\0');
    auto store = makeStore(fileSystem);

    const auto status = store.mount();
    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::InvalidIndex);
}

TEST_CASE("FileStore fails loudly when metadata exists but spool is missing") {
    auto fileSystem = makeFakeFileSystem();
    {
        auto store = makeStore(fileSystem);
        REQUIRE(store.writeIndex({0, 1, 1}).ok());
    }
    REQUIRE(fileSystem->exists("pqueue.spool"));
    fileSystem->files.erase("pqueue.spool");

    auto reopened = makeStore(fileSystem);
    const auto status = reopened.mount();
    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::ReadFailed);
}

TEST_CASE("FileStore fails loudly when spool size does not match layout") {
    auto fileSystem = makeFakeFileSystem();
    {
        auto store = makeStore(fileSystem);
        REQUIRE(store.writeIndex({0, 1, 1}).ok());
    }
    fileSystem->files["pqueue.spool"].resize(slotSize() * 2);

    auto reopened = makeStore(fileSystem);
    const auto status = reopened.mount();
    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::InvalidIndex);
}



#endif // !ARDUINO
