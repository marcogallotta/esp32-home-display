#include "pqueue/file_store.h"

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
    bool mount(const std::string& basePath) override {
        mountedBasePath = basePath;
        return !failMountOnce.consume();
    }

    bool readFile(const std::string& name, std::string& out) override {
        if (failReadOnce.consume()) {
            return false;
        }
        const auto it = files.find(name);
        if (it == files.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool writeFile(const std::string& name, const std::string& data) override {
        if (failWriteOnce.consume()) {
            return false;
        }

        const auto existing = files.find(name);
        const std::size_t existingSize = existing == files.end() ? 0 : existing->second.size();
        if (usedBytes() - existingSize + data.size() > capacityBytes) {
            return false;
        }

        files[name] = data;
        return true;
    }

    bool removeFile(const std::string& name) override {
        if (failRemoveOnce.consume()) {
            return false;
        }
        files.erase(name);
        return true;
    }

    bool renameFile(const std::string& fromName, const std::string& toName) override {
        if (failRenameOnce.consume()) {
            return false;
        }
        const auto it = files.find(fromName);
        if (it == files.end()) {
            return false;
        }
        files[toName] = it->second;
        files.erase(it);
        return true;
    }

    bool listFiles(std::vector<std::string>& out) override {
        if (failListOnce.consume()) {
            return false;
        }
        for (const auto& entry : files) {
            out.push_back(entry.first);
        }
        return true;
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
    void failNextList() { failListOnce.arm(); }

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

    void truncateFile(const std::string& name, std::size_t bytes) {
        auto it = files.find(name);
        REQUIRE(it != files.end());
        it->second.resize(std::min(bytes, it->second.size()));
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
    OneShotFailure failListOnce;
    std::size_t capacityBytes = std::numeric_limits<std::size_t>::max();
};

std::shared_ptr<FakeFileSystem> makeFakeFileSystem() {
    return std::make_shared<FakeFileSystem>();
}

pqueue::FileStore makeStore(const std::shared_ptr<FakeFileSystem>& fileSystem) {
    pqueue::FileStoreConfig config;
    config.basePath = "/fake-pqueue";
    config.fileSystem = fileSystem;
    return pqueue::FileStore(config);
}

} // namespace

TEST_CASE("FileStore uses injected file system") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.mount());
    CHECK_EQ(fileSystem->mountedBasePath, "/fake-pqueue");
}

TEST_CASE("FileStore writeRecord fails cleanly when file write fails") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    fileSystem->failNextWrite();

    CHECK_FALSE(store.writeRecord(0, "payload"));
    CHECK_FALSE(fileSystem->exists("pqueue_rec_00000000.bin"));
    CHECK_FALSE(fileSystem->exists("pqueue_rec_00000000.bin.tmp"));
}

TEST_CASE("FileStore atomic record rewrite preserves old record when rename fails") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "old"));
    fileSystem->failNextRename();

    CHECK_FALSE(store.writeRecord(0, "new"));

    std::string out;
    REQUIRE(store.readRecord(0, out));
    CHECK_EQ(out, "old");
    CHECK_FALSE(fileSystem->exists("pqueue_rec_00000000.bin.tmp"));
}

TEST_CASE("FileStore rejects corrupt records") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload"));
    fileSystem->corruptFile("pqueue_rec_00000000.bin");

    std::string out;
    CHECK_FALSE(store.readRecord(0, out));
}

TEST_CASE("FileStore rejects truncated records") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload"));
    fileSystem->truncateFile("pqueue_rec_00000000.bin", 4);

    std::string out;
    CHECK_FALSE(store.readRecord(0, out));
}

TEST_CASE("FileStore falls back to older valid index when latest index is corrupt") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeIndex({0, 1, 1}));
    REQUIRE(store.writeIndex({0, 2, 2}));
    REQUIRE(fileSystem->exists("pqueue_idx_a.bin"));
    REQUIRE(fileSystem->exists("pqueue_idx_b.bin"));

    fileSystem->corruptFile("pqueue_idx_b.bin");

    pqueue::FileStoreIndex out;
    REQUIRE(store.readIndex(out));
    CHECK_EQ(out.head, 0U);
    CHECK_EQ(out.tail, 1U);
    CHECK_EQ(out.count, 1U);
}

TEST_CASE("FileStore readIndex fails when rebuild listing fails") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    fileSystem->failNextList();

    pqueue::FileStoreIndex out;
    CHECK_FALSE(store.readIndex(out));
}

TEST_CASE("FileStore readIndex fails when rebuilt index cannot be persisted") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload"));
    fileSystem->failNextWrite();

    pqueue::FileStoreIndex out;
    CHECK_FALSE(store.readIndex(out));
}

TEST_CASE("FileStore rebuild ignores temporary record files") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    fileSystem->files["pqueue_rec_00000000.bin.tmp"] = "not a committed record";

    pqueue::FileStoreIndex out;
    REQUIRE(store.readIndex(out));
    CHECK_EQ(out.head, 0U);
    CHECK_EQ(out.tail, 0U);
    CHECK_EQ(out.count, 0U);
}

TEST_CASE("FileStore rebuild stops at first missing record") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "zero"));
    REQUIRE(store.writeRecord(2, "two"));

    pqueue::FileStoreIndex out;
    REQUIRE(store.readIndex(out));
    CHECK_EQ(out.head, 0U);
    CHECK_EQ(out.tail, 1U);
    CHECK_EQ(out.count, 1U);
}

TEST_CASE("FileStore capacity failure leaves no valid committed record") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    fileSystem->setCapacityBytes(8);

    CHECK_FALSE(store.writeRecord(0, "payload"));
    CHECK_FALSE(fileSystem->exists("pqueue_rec_00000000.bin"));
    CHECK_FALSE(fileSystem->exists("pqueue_rec_00000000.bin.tmp"));
}

TEST_CASE("FileStore removeRecord reports remove failure") {
    auto fileSystem = makeFakeFileSystem();
    auto store = makeStore(fileSystem);

    REQUIRE(store.writeRecord(0, "payload"));
    fileSystem->failNextRemove();

    CHECK_FALSE(store.removeRecord(0));
    CHECK(fileSystem->exists("pqueue_rec_00000000.bin"));
}

#endif // !ARDUINO
