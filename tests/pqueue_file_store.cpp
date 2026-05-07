#include "pqueue/file_store.h"
#include "pqueue/queue.h"
#include "pqueue/storage_common.h"

#include "doctest/doctest.h"

#ifndef ARDUINO

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

class FakeFileSystem final : public pqueue::FileSystem {
public:
    pqueue::Status mount(const std::string& basePath) override {
        mountedBasePath = basePath;
        if (failMountOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::MountFailed, "fake mount failed");
        }
        return pqueue::Status::success();
    }

    pqueue::Status readFile(const std::string& name, std::string& out) override {
        if (failReadOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake read failed");
        }
        const auto it = files.find(name);
        if (it == files.end()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake file missing");
        }
        out = it->second;
        return pqueue::Status::success();
    }

    pqueue::Status writeFile(const std::string& name, const std::string& data) override {
        if (failWriteOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake write failed");
        }
        const auto existing = files.find(name);
        const std::size_t existingSize = existing == files.end() ? 0 : existing->second.size();
        if (usedBytes() - existingSize + data.size() > capacityBytes) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake capacity exceeded");
        }
        if (partialWriteFileOnce.armed) {
            const auto bytes = std::min(partialWriteFileOnce.bytes, data.size());
            files[name] = data.substr(0, bytes);
            partialWriteFileOnce.clear();
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake partial write failed");
        }
        files[name] = data;
        return pqueue::Status::success();
    }

    pqueue::Status readAt(const std::string& name, std::uint64_t offset, std::size_t size, std::string& out) override {
        if (failReadOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake readAt failed");
        }
        const auto it = files.find(name);
        if (it == files.end() || offset + size > it->second.size()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake readAt range missing");
        }
        out = it->second.substr(static_cast<std::size_t>(offset), size);
        return pqueue::Status::success();
    }

    pqueue::Status writeAt(const std::string& name, std::uint64_t offset, const std::string& data) override {
        if (failWriteOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake writeAt failed");
        }
        auto it = files.find(name);
        if (it == files.end() || offset + data.size() > it->second.size()) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake writeAt range missing");
        }
        if (partialWriteAtOnce.armed) {
            const auto bytes = std::min(partialWriteAtOnce.bytes, data.size());
            it->second.replace(static_cast<std::size_t>(offset), bytes, data.substr(0, bytes));
            partialWriteAtOnce.clear();
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake partial writeAt failed");
        }
        it->second.replace(static_cast<std::size_t>(offset), data.size(), data);
        return pqueue::Status::success();
    }

    pqueue::Status resizeFile(const std::string& name, std::uint64_t size) override {
        if (failWriteOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake resize failed");
        }
        const auto existing = files.find(name);
        const std::size_t existingSize = existing == files.end() ? 0 : existing->second.size();
        if (usedBytes() - existingSize + size > capacityBytes) {
            return pqueue::Status::failure(pqueue::StatusCode::WriteFailed, "fake capacity exceeded");
        }
        files[name].resize(static_cast<std::size_t>(size), '\0');
        return pqueue::Status::success();
    }

    pqueue::Status fileSize(const std::string& name, std::uint64_t& out) override {
        const auto it = files.find(name);
        if (it == files.end()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake stat missing");
        }
        out = static_cast<std::uint64_t>(it->second.size());
        return pqueue::Status::success();
    }

    pqueue::Status removeFile(const std::string& name) override {
        if (failRemoveOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::RemoveFailed, "fake remove failed");
        }
        files.erase(name);
        return pqueue::Status::success();
    }

    pqueue::Status renameFile(const std::string& fromName, const std::string& toName) override {
        if (failRenameOnce.consume()) {
            return pqueue::Status::failure(pqueue::StatusCode::RenameFailed, "fake rename failed");
        }
        const auto it = files.find(fromName);
        if (it == files.end()) {
            return pqueue::Status::failure(pqueue::StatusCode::RenameFailed, "fake source missing");
        }
        files[toName] = it->second;
        files.erase(it);
        return pqueue::Status::success();
    }

    pqueue::Status listFiles(std::vector<std::string>& out) override {
        for (const auto& entry : files) {
            out.push_back(entry.first);
        }
        return pqueue::Status::success();
    }

    pqueue::Status tryAcquireLockFile(const std::string& name, const std::string& contents) override {
        if (files.find(name) != files.end()) {
            return pqueue::Status::failure(pqueue::StatusCode::LockTimeout, "fake lock exists");
        }
        files[name] = contents;
        return pqueue::Status::success();
    }

    pqueue::Status releaseLockFile(const std::string& name, const std::string& expectedContents) override {
        const auto it = files.find(name);
        if (it == files.end()) {
            return pqueue::Status::failure(pqueue::StatusCode::ReadFailed, "fake lock missing");
        }
        if (it->second != expectedContents) {
            return pqueue::Status::failure(pqueue::StatusCode::LockTimeout, "fake lock owned by another queue");
        }
        files.erase(it);
        return pqueue::Status::success();
    }

    std::uint64_t freeBytes() const override {
        const auto used = usedBytes();
        return used < capacityBytes ? capacityBytes - used : 0;
    }

    void failNextMount() { failMountOnce.arm(); }
    void failNextRead() { failReadOnce.arm(); }
    void failNextWrite() { failWriteOnce.arm(); }
    void failNextRemove() { failRemoveOnce.arm(); }
    void failNextRename() { failRenameOnce.arm(); }
    void partialNextWriteFile(std::size_t bytes) { partialWriteFileOnce.arm(bytes); }
    void partialNextWriteAt(std::size_t bytes) { partialWriteAtOnce.arm(bytes); }

    void setCapacityBytes(std::size_t bytes) { capacityBytes = bytes; }

    bool exists(const std::string& name) const {
        return files.find(name) != files.end();
    }

    void corruptFile(const std::string& name) {
        auto it = files.find(name);
        REQUIRE(it != files.end());
        REQUIRE_FALSE(it->second.empty());
        it->second[it->second.size() / 2] ^= static_cast<char>(0xff);
    }

    void corruptSlotHeader(std::uint32_t slot, std::size_t slotSize) {
        auto it = files.find("pqueue.spool");
        REQUIRE(it != files.end());
        const auto offset = pqueue::storage_detail::kCheckpointSlots * pqueue::storage_detail::kCheckpointRecordBytes + 4096 + slot * slotSize;
        REQUIRE(it->second.size() >= offset + 1);
        it->second[offset] ^= static_cast<char>(0xff);
    }

    void corruptSlotPayload(std::uint32_t slot, std::size_t slotSize) {
        auto it = files.find("pqueue.spool");
        REQUIRE(it != files.end());
        const auto offset = pqueue::storage_detail::kCheckpointSlots * pqueue::storage_detail::kCheckpointRecordBytes + 4096 + slot * slotSize + pqueue::storage_detail::kRecordHeaderBytes;
        REQUIRE(it->second.size() > offset);
        it->second[offset] ^= static_cast<char>(0xff);
    }

    std::map<std::string, std::string> files;
    std::string mountedBasePath;

private:
    struct OneShotFailure {
        void arm() { armed = true; }
        bool consume() {
            if (!armed) {
                return false;
            }
            armed = false;
            return true;
        }
        bool armed = false;
    };

    struct OneShotPartialWrite {
        void arm(std::size_t prefixBytes) {
            armed = true;
            bytes = prefixBytes;
        }
        void clear() {
            armed = false;
            bytes = 0;
        }
        bool armed = false;
        std::size_t bytes = 0;
    };

    std::size_t usedBytes() const {
        std::size_t out = 0;
        for (const auto& entry : files) {
            out += entry.second.size();
        }
        return out;
    }

    OneShotFailure failMountOnce;
    OneShotFailure failReadOnce;
    OneShotFailure failWriteOnce;
    OneShotFailure failRemoveOnce;
    OneShotFailure failRenameOnce;
    OneShotPartialWrite partialWriteFileOnce;
    OneShotPartialWrite partialWriteAtOnce;
    std::size_t capacityBytes = std::numeric_limits<std::size_t>::max();
};

struct CapturedEvent {
    pqueue::EventKind kind = pqueue::EventKind::Diagnostic;
    pqueue::Severity severity = pqueue::Severity::Debug;
    pqueue::StatusCode code = pqueue::StatusCode::Ok;
};

void captureEvent(const pqueue::Event& event, void* user) {
    auto* events = static_cast<std::vector<CapturedEvent>*>(user);
    events->push_back(CapturedEvent{event.kind, event.severity, event.status.code});
}

std::shared_ptr<FakeFileSystem> makeFakeFileSystem() {
    return std::make_shared<FakeFileSystem>();
}

pqueue::FileStore makeStore(const std::shared_ptr<FakeFileSystem>& fileSystem, pqueue::EventOptions events = {}, std::uint32_t reservedBytes = 160, std::size_t recordSizeBytes = 32, std::uint32_t checkpointEveryOps = 64) {
    pqueue::FileStoreConfig config;
    config.basePath = "/fake-pqueue";
    config.fileSystem = fileSystem;
    config.events = events;
    config.reservedBytes = reservedBytes;
    config.recordSizeBytes = recordSizeBytes;
    config.checkpointEveryOps = checkpointEveryOps;
    return pqueue::FileStore(config);
}

pqueue::Config makeQueueConfig(const std::shared_ptr<FakeFileSystem>& fileSystem, pqueue::EventOptions events = {}, std::uint32_t reservedBytes = 160, std::size_t recordSizeBytes = 32, std::uint32_t checkpointEveryOps = 64) {
    pqueue::Config config;
    config.basePath = "/fake-pqueue";
    config.fileSystem = fileSystem;
    config.events = events;
    config.reservedBytes = reservedBytes;
    config.recordSizeBytes = recordSizeBytes;
    config.checkpointEveryOps = checkpointEveryOps;
    return config;
}

std::size_t slotSize(std::size_t recordSizeBytes = 32) {
    return pqueue::storage_detail::kRecordHeaderBytes + recordSizeBytes;
}

std::size_t spoolSize(std::uint32_t reservedBytes = 160, std::size_t recordSizeBytes = 32, std::uint32_t journalBytes = 4096) {
    const auto slots = reservedBytes / slotSize(recordSizeBytes);
    return pqueue::storage_detail::kCheckpointSlots * pqueue::storage_detail::kCheckpointRecordBytes +
           journalBytes +
           slots * slotSize(recordSizeBytes);
}

} // namespace

TEST_CASE("FileStore mounts and preallocates one spool file") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.mount().ok());
    CHECK_EQ(fileSystem->mountedBasePath, "/fake-pqueue");
    REQUIRE(fileSystem->exists("pqueue.spool"));
    CHECK_EQ(fileSystem->files["pqueue.spool"].size(), spoolSize());
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

TEST_CASE("FileStore keeps the older valid checkpoint when the latest is corrupt") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, 160, 32, 1);

    REQUIRE(store.writeIndex({0, 1, 1}).ok());
    REQUIRE(store.writeIndex({0, 2, 2}).ok());
    REQUIRE(fileSystem->exists("pqueue.spool"));

    const auto latestCheckpointOffset = 3U * pqueue::storage_detail::kCheckpointRecordBytes;
    fileSystem->files["pqueue.spool"][latestCheckpointOffset] ^= static_cast<char>(0xff);

    auto reopened = makeStore(fileSystem, {}, 160, 32, 1);
    pqueue::FileStoreIndex out;
    REQUIRE(reopened.readIndex(out).ok());
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
    CHECK_EQ(fileSystem->files["pqueue.spool"].size(), spoolSize(static_cast<std::uint32_t>(slotSize() * 2 + 7), 32));
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

TEST_CASE("Queue does not advance index after torn record write") {
    auto fileSystem = makeFakeFileSystem();

    {
        auto store = makeStore(fileSystem);
        REQUIRE(store.mount().ok());
    }

    {
        auto queue = pqueue::Queue(makeQueueConfig(fileSystem));
        fileSystem->partialNextWriteAt(pqueue::storage_detail::kRecordHeaderBytes / 2);
        const auto status = queue.enqueue("first");
        REQUIRE_FALSE(status.ok());
        CHECK(status.code == pqueue::StatusCode::WriteFailed);
    }

    {
        auto store = makeStore(fileSystem);
        pqueue::FileStoreIndex index;
        REQUIRE(store.readIndex(index).ok());
        CHECK_EQ(index.head, 0U);
        CHECK_EQ(index.tail, 0U);
        CHECK_EQ(index.count, 0U);
    }

    {
        auto queue = pqueue::Queue(makeQueueConfig(fileSystem));
        std::string out;
        CHECK(queue.peek(out).code == pqueue::StatusCode::QueueEmpty);

        REQUIRE(queue.enqueue("second").ok());
        REQUIRE(queue.peek(out).ok());
        CHECK_EQ(out, "second");

        const auto validation = queue.validate();
        CHECK(validation.ok);
        CHECK_EQ(validation.checkedRecords, 1U);
    }
}


TEST_CASE("FileStore fails loudly when spool exists without metadata") {
    auto fileSystem = makeFakeFileSystem();
    fileSystem->files["pqueue.spool"] = std::string(spoolSize(), '\0');
    auto store = makeStore(fileSystem);

    const auto status = store.mount();
    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::InvalidIndex);
}

TEST_CASE("FileStore recreates fresh storage when the single spool file is missing") {
    auto fileSystem = makeFakeFileSystem();
    {
        auto store = makeStore(fileSystem);
        REQUIRE(store.writeIndex({0, 1, 1}).ok());
    }
    REQUIRE(fileSystem->exists("pqueue.spool"));
    fileSystem->files.erase("pqueue.spool");

    auto reopened = makeStore(fileSystem);
    REQUIRE(reopened.mount().ok());
    pqueue::FileStoreIndex out;
    REQUIRE(reopened.readIndex(out).ok());
    CHECK_EQ(out.count, 0U);
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



TEST_CASE("FileStore fails loudly when all checkpoint slots are corrupt") {
    auto fileSystem = makeFakeFileSystem();
    {
        auto store = makeStore(fileSystem, {}, 160, 32, 1);
        REQUIRE(store.writeIndex({0, 1, 1}).ok());
        REQUIRE(store.writeIndex({0, 2, 2}).ok());
        REQUIRE(store.writeIndex({0, 3, 3}).ok());
    }

    REQUIRE(fileSystem->exists("pqueue.spool"));
    for (std::uint32_t slot = 0; slot < pqueue::storage_detail::kCheckpointSlots; ++slot) {
        const auto offset = slot * pqueue::storage_detail::kCheckpointRecordBytes;
        fileSystem->files["pqueue.spool"][offset] ^= static_cast<char>(0xff);
    }

    auto reopened = makeStore(fileSystem, {}, 160, 32, 1);
    pqueue::FileStoreIndex out;
    const auto status = reopened.readIndex(out);

    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::InvalidIndex);
}

TEST_CASE("FileStore rejects index transition when checkpoint write fails") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, 160, 32, 1);

    REQUIRE(store.writeIndex({0, 1, 1}).ok());
    pqueue::FileStoreIndex before;
    REQUIRE(store.readIndex(before).ok());
    CHECK_EQ(before.count, 1U);

    fileSystem->failNextWrite();
    const auto status = store.writeIndex({0, 2, 2});

    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::WriteFailed);

    pqueue::FileStoreIndex after;
    REQUIRE(store.readIndex(after).ok());
    CHECK_EQ(after.head, before.head);
    CHECK_EQ(after.tail, before.tail);
    CHECK_EQ(after.count, before.count);
}

TEST_CASE("FileStore rejects index transition when journal append fails") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem, {}, 160, 32, 64);

    REQUIRE(store.writeIndex({0, 1, 1}).ok());
    pqueue::FileStoreIndex before;
    REQUIRE(store.readIndex(before).ok());
    CHECK_EQ(before.count, 1U);

    fileSystem->failNextWrite();
    const auto status = store.writeIndex({0, 2, 2});

    REQUIRE_FALSE(status.ok());
    CHECK(status.code == pqueue::StatusCode::WriteFailed);

    pqueue::FileStoreIndex after;
    REQUIRE(store.readIndex(after).ok());
    CHECK_EQ(after.head, before.head);
    CHECK_EQ(after.tail, before.tail);
    CHECK_EQ(after.count, before.count);
}


#endif // !ARDUINO
