#include "api/buffer.h"
#include "api/request_file_store.h"

#include "doctest/doctest.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kSpoolDir = "spool";
const std::filesystem::path kIndexAPath = "spool/api_idx_a.bin";
const std::filesystem::path kIndexBPath = "spool/api_idx_b.bin";

void cleanSpoolDirectory() {
#ifndef ARDUINO
    std::error_code ec;
    std::filesystem::remove_all(kSpoolDir, ec);
    std::filesystem::create_directory(kSpoolDir, ec);
#endif
}

std::filesystem::path bufferedRequestPath(std::uint32_t sequence) {
    char name[64];
    std::snprintf(name, sizeof(name), "spool/api_req_%08lu.bin", static_cast<unsigned long>(sequence));
    return name;
}

api::BufferedRequest switchBotReadingNamed(const std::string& readingName) {
    api::BufferedRequest request;
    request.path = "/switchbot/reading";
    request.mac = "";
    request.body = "{\"mac\":\"EC:2E:84:06:4E:9A\",\"name\":\"" + readingName + "\",\"type\":\"switchbot\"}";
    request.timeoutRetryCount = 2;
    request.tlsRetryCount = 1;
    return request;
}

api::BufferedRequest readingWithBody(std::string body) {
    api::BufferedRequest request;
    request.path = "/switchbot/reading";
    request.body = std::move(body);
    return request;
}

std::string extractNameFromBody(const std::string& body) {
    const std::string marker = "\"name\":\"";
    const auto start = body.find(marker);
    if (start == std::string::npos) {
        return "";
    }

    const auto valueStart = start + marker.size();
    const auto valueEnd = body.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return "";
    }

    return body.substr(valueStart, valueEnd - valueStart);
}

void flipLastByteOfExistingFile(const std::filesystem::path& path) {
#ifndef ARDUINO
    REQUIRE(std::filesystem::exists(path));

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(file.good());

    file.seekg(-1, std::ios::end);
    char byte = 0;
    file.read(&byte, 1);
    REQUIRE(file.good());

    file.clear();
    file.seekp(-1, std::ios::end);
    byte ^= 0x01;
    file.write(&byte, 1);
    REQUIRE(file.good());
#endif
}

class RequestStoreScenario {
public:
    static RequestStoreScenario emptySpool() {
        cleanSpoolDirectory();
        return RequestStoreScenario();
    }

    void writeBufferedSwitchBotReading(const std::string& readingName) {
        REQUIRE(api::request_file_store::writeRequest(nextSequence_, switchBotReadingNamed(readingName)));
        sequenceNames_.push_back(readingName);
        ++nextSequence_;
    }

    void deleteQueueIndexFiles() {
#ifndef ARDUINO
        std::error_code ec;
        std::filesystem::remove(kIndexAPath, ec);
        std::filesystem::remove(kIndexBPath, ec);
#endif
    }

    void corruptQueueIndexFile() {
        writeBothQueueIndexCopiesForAllCurrentRequestFiles();
#ifndef ARDUINO
        const bool indexAExists = std::filesystem::exists(kIndexAPath);
        const bool indexBExists = std::filesystem::exists(kIndexBPath);
        REQUIRE((indexAExists || indexBExists));

        if (indexAExists) {
            flipLastByteOfExistingFile(kIndexAPath);
        }
        if (indexBExists) {
            flipLastByteOfExistingFile(kIndexBPath);
        }
#endif
    }

    void corruptOnlyOlderQueueIndexCopy() {
        writeBothQueueIndexCopiesForAllCurrentRequestFiles();
#ifndef ARDUINO
        REQUIRE(std::filesystem::exists(kIndexAPath));
        flipLastByteOfExistingFile(kIndexAPath);
#endif
    }

    void corruptOnlyNewerQueueIndexCopy() {
        writeBothQueueIndexCopiesForAllCurrentRequestFiles();
#ifndef ARDUINO
        REQUIRE(std::filesystem::exists(kIndexBPath));
        flipLastByteOfExistingFile(kIndexBPath);
#endif
    }

    void writeQueueIndexThatOnlyKnowsAboutTheFirstReading() {
        REQUIRE(sequenceNames_.size() >= 2);
        writeQueueIndexForFirstReadings(1);
    }

    void writeQueueIndexThatClaimsAllCurrentReadingsExist() {
        writeQueueIndexForAllCurrentRequestFiles();
    }

    void deleteBufferedReadingFile(const std::string& readingName) {
#ifndef ARDUINO
        const auto sequence = sequenceForName(readingName);
        std::error_code ec;
        std::filesystem::remove(bufferedRequestPath(sequence), ec);
#else
        (void)readingName;
#endif
    }

    void writeEmptyTrailingRequestFile() {
#ifndef ARDUINO
        std::ofstream file(bufferedRequestPath(nextSequence_), std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        ++nextSequence_;
#else
        ++nextSequence_;
#endif
    }

    void writeTruncatedTrailingRequestFile() {
#ifndef ARDUINO
        const auto request = switchBotReadingNamed("truncated trailing reading");
        REQUIRE(api::request_file_store::writeRequest(nextSequence_, request));
        std::filesystem::resize_file(bufferedRequestPath(nextSequence_), 8);
        ++nextSequence_;
#else
        ++nextSequence_;
#endif
    }

    void corruptBufferedReading(const std::string& readingName) {
#ifndef ARDUINO
        const auto sequence = sequenceForName(readingName);
        flipLastByteOfExistingFile(bufferedRequestPath(sequence));
#else
        (void)readingName;
#endif
    }

    void restartStoreFromDisk() {
        recoveredIndex_ = api::request_file_store::Index{};
        REQUIRE(api::request_file_store::readIndex(recoveredIndex_));
    }

    std::vector<std::string> queuedReadingNames() const {
        std::vector<std::string> names;
        for (std::uint32_t sequence = recoveredIndex_.head; sequence < recoveredIndex_.tail; ++sequence) {
            api::BufferedRequest request;
            if (api::request_file_store::readRequest(sequence, request)) {
                names.push_back(extractNameFromBody(request.body));
            }
        }
        return names;
    }

    void enqueueAfterRestart(const std::string& readingName) {
        REQUIRE(api::request_file_store::writeRequest(recoveredIndex_.tail, switchBotReadingNamed(readingName)));
        recoveredIndex_.tail += 1;
        recoveredIndex_.count += 1;
        REQUIRE(api::request_file_store::writeIndex(recoveredIndex_));
    }

private:
    RequestStoreScenario() = default;

    void writeQueueIndexForAllCurrentRequestFiles() const {
        writeQueueIndexForFirstReadings(nextSequence_);
    }

    void writeBothQueueIndexCopiesForAllCurrentRequestFiles() const {
        writeQueueIndexForAllCurrentRequestFiles();
        writeQueueIndexForAllCurrentRequestFiles();
    }

    void writeQueueIndexForFirstReadings(std::uint32_t readingCount) const {
        api::request_file_store::Index index;
        index.head = 0;
        index.tail = readingCount;
        index.count = readingCount;
        REQUIRE(api::request_file_store::writeIndex(index));
    }

    std::uint32_t sequenceForName(const std::string& readingName) const {
        for (std::uint32_t sequence = 0; sequence < sequenceNames_.size(); ++sequence) {
            if (sequenceNames_[sequence] == readingName) {
                return sequence;
            }
        }
        FAIL("test tried to corrupt a buffered reading that was never written: " << readingName);
        return 0;
    }

    std::uint32_t nextSequence_ = 0;
    std::vector<std::string> sequenceNames_;
    api::request_file_store::Index recoveredIndex_;
};

class SingleRequestFileScenario {
public:
    static SingleRequestFileScenario emptySpool() {
        cleanSpoolDirectory();
        return SingleRequestFileScenario();
    }

    void writeReadingToDisk(api::BufferedRequest request) {
        original_ = std::move(request);
        REQUIRE(api::request_file_store::writeRequest(kSequence, original_));
    }

    void writeSwitchBotReadingToDisk(const std::string& readingName) {
        writeReadingToDisk(switchBotReadingNamed(readingName));
    }

    api::BufferedRequest readReadingBackFromDisk() const {
        api::BufferedRequest loaded;
        REQUIRE(api::request_file_store::readRequest(kSequence, loaded));
        return loaded;
    }

    const api::BufferedRequest& originalReading() const {
        return original_;
    }

private:
    static constexpr std::uint32_t kSequence = 23;
    api::BufferedRequest original_;
};

void expectQueuedReadings(RequestStoreScenario& store, std::vector<std::string> expectedNames) {
    CHECK_EQ(store.queuedReadingNames(), expectedNames);
}

} // namespace

TEST_CASE("request file store preserves the fields needed to resend a buffered request") {
    auto file = SingleRequestFileScenario::emptySpool();

    file.writeSwitchBotReadingToDisk("Bed");

    const auto loaded = file.readReadingBackFromDisk();
    CHECK_EQ(loaded.path, file.originalReading().path);
    CHECK_EQ(loaded.body, file.originalReading().body);
    CHECK_EQ(loaded.timeoutRetryCount, 2);
    CHECK_EQ(loaded.tlsRetryCount, 1);
}

TEST_CASE("request file store hydrates diagnostic mac from the persisted request body") {
    auto file = SingleRequestFileScenario::emptySpool();

    file.writeSwitchBotReadingToDisk("Bed");

    const auto loaded = file.readReadingBackFromDisk();
    CHECK_EQ(loaded.mac, "EC:2E:84:06:4E:9A");
}

TEST_CASE("request file store uses the body mac instead of stale in-memory metadata") {
    auto file = SingleRequestFileScenario::emptySpool();
    auto request = readingWithBody("{\"mac\":\"11:22:33:44:55:66\",\"name\":\"Bed\",\"type\":\"switchbot\"}");
    request.mac = "AA:BB:CC:DD:EE:FF";

    file.writeReadingToDisk(request);

    const auto loaded = file.readReadingBackFromDisk();
    CHECK_EQ(loaded.mac, "11:22:33:44:55:66");
    CHECK_EQ(loaded.body, request.body);
}

TEST_CASE("request file store leaves diagnostic mac empty when the body has no mac") {
    auto file = SingleRequestFileScenario::emptySpool();

    file.writeReadingToDisk(readingWithBody("{\"name\":\"test\"}"));

    const auto loaded = file.readReadingBackFromDisk();
    CHECK(loaded.mac.empty());
    CHECK_EQ(loaded.body, "{\"name\":\"test\"}");
}

TEST_CASE("request file store leaves diagnostic mac empty when the body is not JSON") {
    auto file = SingleRequestFileScenario::emptySpool();

    file.writeReadingToDisk(readingWithBody("not-json"));

    const auto loaded = file.readReadingBackFromDisk();
    CHECK(loaded.mac.empty());
    CHECK_EQ(loaded.body, "not-json");
}

TEST_CASE("request store recovers buffered readings when the queue index is missing") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("Bed before outage");
    store.writeBufferedSwitchBotReading("Kitchen before outage");
    store.deleteQueueIndexFiles();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"Bed before outage", "Kitchen before outage"});
}

TEST_CASE("request store recovers buffered readings when the queue index is corrupt") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("Bed before outage");
    store.writeBufferedSwitchBotReading("Kitchen before outage");
    store.corruptQueueIndexFile();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"Bed before outage", "Kitchen before outage"});
}

TEST_CASE("request store falls back to the newer index copy when the older copy is corrupt") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("Bed before outage");
    store.writeBufferedSwitchBotReading("Kitchen before outage");
    store.corruptOnlyOlderQueueIndexCopy();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"Bed before outage", "Kitchen before outage"});
}

TEST_CASE("request store falls back to the older index copy when the newer copy is corrupt") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("Bed before outage");
    store.writeBufferedSwitchBotReading("Kitchen before outage");
    store.corruptOnlyNewerQueueIndexCopy();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"Bed before outage", "Kitchen before outage"});
}

TEST_CASE("request store recovers a valid buffered reading that was written but never indexed") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("indexed reading");
    store.writeBufferedSwitchBotReading("written but not indexed reading");
    store.writeQueueIndexThatOnlyKnowsAboutTheFirstReading();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"indexed reading", "written but not indexed reading"});
}

TEST_CASE("request store ignores a missing trailing request file that the index still mentions") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("surviving reading");
    store.writeBufferedSwitchBotReading("missing trailing reading");
    store.writeQueueIndexThatClaimsAllCurrentReadingsExist();
    store.deleteBufferedReadingFile("missing trailing reading");

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"surviving reading"});
}

TEST_CASE("request store ignores an empty trailing request file and keeps earlier readings") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("surviving reading");
    store.writeEmptyTrailingRequestFile();
    store.deleteQueueIndexFiles();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"surviving reading"});
}

TEST_CASE("request store ignores a truncated trailing request file and keeps earlier readings") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("surviving reading");
    store.writeTruncatedTrailingRequestFile();
    store.deleteQueueIndexFiles();

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"surviving reading"});
}

TEST_CASE("request store ignores a corrupt trailing reading and keeps earlier readings") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("first valid reading");
    store.writeBufferedSwitchBotReading("second valid reading");
    store.writeBufferedSwitchBotReading("corrupt trailing reading");
    store.deleteQueueIndexFiles();
    store.corruptBufferedReading("corrupt trailing reading");

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"first valid reading", "second valid reading"});
}

TEST_CASE("request store stops recovery before readings after a corrupt middle reading") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("first valid reading");
    store.writeBufferedSwitchBotReading("corrupt middle reading");
    store.writeBufferedSwitchBotReading("unreachable later reading");
    store.deleteQueueIndexFiles();
    store.corruptBufferedReading("corrupt middle reading");

    store.restartStoreFromDisk();

    expectQueuedReadings(store, {"first valid reading"});
}

TEST_CASE("request store can enqueue again after recovering from a corrupt trailing reading") {
    auto store = RequestStoreScenario::emptySpool();

    store.writeBufferedSwitchBotReading("surviving reading");
    store.writeBufferedSwitchBotReading("corrupt trailing reading");
    store.deleteQueueIndexFiles();
    store.corruptBufferedReading("corrupt trailing reading");

    store.restartStoreFromDisk();
    store.enqueueAfterRestart("new reading after recovery");

    expectQueuedReadings(store, {"surviving reading", "new reading after recovery"});
}
