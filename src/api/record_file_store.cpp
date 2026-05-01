#include "record_file_store.h"

#include "buffer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>


#include "../log.h"

#ifdef ARDUINO
#include <LittleFS.h>
#else
#include <filesystem>
#include <fstream>
#endif

namespace api::record_file_store {
namespace {

constexpr std::uint32_t kIndexMagic = 0x50514958;   // PQIX
constexpr std::uint32_t kRecordMagic = 0x50515243; // PQRC
constexpr std::uint16_t kFormatVersion = 1;

constexpr std::size_t kMaxPathBytes = 96;
constexpr std::size_t kMaxFilePathBytes = 128;
constexpr std::size_t kMaxPayloadBytes = 4096;

#ifdef ARDUINO
constexpr const char* kIndexAPath = "/pqueue_idx_a.bin";
constexpr const char* kIndexBPath = "/pqueue_idx_b.bin";
#else
constexpr const char* kIndexAFileName = "pqueue_idx_a.bin";
constexpr const char* kIndexBFileName = "pqueue_idx_b.bin";
#endif

struct IndexRecord {
    std::uint32_t magic = kIndexMagic;
    std::uint16_t formatVersion = kFormatVersion;
    std::uint16_t recordBytes = sizeof(IndexRecord);
    std::uint32_t version = 0;
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    std::uint32_t crc = 0;
};

struct RecordHeader {
    std::uint32_t magic = kRecordMagic;
    std::uint16_t formatVersion = kFormatVersion;
    std::uint16_t headerBytes = sizeof(RecordHeader);
    std::uint16_t pathBytes = 0;
    std::uint16_t reserved = 0;
    std::uint32_t payloadBytes = 0;
    std::int32_t timeoutRetryCount = 0;
    std::int32_t tlsRetryCount = 0;
    std::uint32_t crc = 0;
};

std::uint32_t crc32Update(std::uint32_t crc, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    crc = ~crc;

    for (std::size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }

    return ~crc;
}

std::uint32_t indexCrc(const IndexRecord& record) {
    return crc32Update(0, &record, offsetof(IndexRecord, crc));
}

std::uint32_t recordCrc(
    const RecordHeader& header,
    const char* path,
    const char* payload
) {
    std::uint32_t crc = crc32Update(0, &header, offsetof(RecordHeader, crc));
    crc = crc32Update(crc, path, header.pathBytes);
    crc = crc32Update(crc, payload, header.payloadBytes);
    return crc;
}

#ifndef ARDUINO
std::string& basePath() {
    static std::string path = "spool";
    return path;
}

std::string storePath(const char* filename) {
    return basePath() + "/" + filename;
}
#endif

bool makeRecordPath(std::uint32_t sequence, char* out, std::size_t outSize) {
#ifdef ARDUINO
    const int n = std::snprintf(out, outSize, "/pqueue_rec_%08lu.bin", static_cast<unsigned long>(sequence));
#else
    const int n = std::snprintf(
        out,
        outSize,
        "%s/pqueue_rec_%08lu.bin",
        basePath().c_str(),
        static_cast<unsigned long>(sequence)
    );
#endif
    return n > 0 && static_cast<std::size_t>(n) < outSize;
}


bool parseRecordSequenceFromName(const char* name, std::uint32_t& out) {
    if (name == nullptr) {
        return false;
    }

    unsigned long value = 0;
    char suffix = '\0';
    if (std::sscanf(name, "pqueue_rec_%08lu.bin%c", &value, &suffix) != 1) {
        return false;
    }

    if (value > 0xFFFFFFFFul) {
        return false;
    }

    out = static_cast<std::uint32_t>(value);
    return true;
}

bool recordFileExists(std::uint32_t sequence, const std::set<std::uint32_t>& sequences) {
    return sequences.find(sequence) != sequences.end();
}


#ifdef ARDUINO

bool mounted = false;

bool writeAll(File& file, const void* data, std::size_t len) {
    return file.write(static_cast<const std::uint8_t*>(data), len) == len;
}

bool readAll(File& file, void* data, std::size_t len) {
    return file.readBytes(static_cast<char*>(data), len) == len;
}

bool readIndexRecord(const char* path, IndexRecord& out) {
    struct stat st {};
    if (stat(path, &st) != 0) {
        return false;
    }

    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    const bool ok = file.size() == sizeof(IndexRecord) &&
                    readAll(file, &out, sizeof(out));
    file.close();

    if (!ok) {
        return false;
    }

    return out.magic == kIndexMagic &&
           out.formatVersion == kFormatVersion &&
           out.recordBytes == sizeof(IndexRecord) &&
           out.crc == indexCrc(out);
}

bool writeIndexRecord(const char* path, const IndexRecord& record) {
    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }

    const bool ok = writeAll(file, &record, sizeof(record));
    file.close();
    return ok;
}

#else

bool writeAll(std::ofstream& file, const void* data, std::size_t len) {
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(len));
    return static_cast<bool>(file);
}

bool readAll(std::ifstream& file, void* data, std::size_t len) {
    file.read(static_cast<char*>(data), static_cast<std::streamsize>(len));
    return file.gcount() == static_cast<std::streamsize>(len);
}

bool readIndexRecord(const char* path, IndexRecord& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    if (!readAll(file, &out, sizeof(out))) {
        return false;
    }

    return out.magic == kIndexMagic &&
           out.formatVersion == kFormatVersion &&
           out.recordBytes == sizeof(IndexRecord) &&
           out.crc == indexCrc(out);
}

bool writeIndexRecord(const char* path, const IndexRecord& record) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }

    return writeAll(file, &record, sizeof(record));
}

#endif


bool listRecordSequences(std::vector<std::uint32_t>& out) {
    out.clear();

#ifdef ARDUINO
    File root = LittleFS.open("/");
    if (!root) {
        return false;
    }

    File file = root.openNextFile();
    while (file) {
        const char* rawName = file.name();
        const char* name = rawName;
        if (name != nullptr && name[0] == '/') {
            ++name;
        }

        std::uint32_t sequence = 0;
        if (parseRecordSequenceFromName(name, sequence)) {
            out.push_back(sequence);
        }

        file.close();
        file = root.openNextFile();
    }

    root.close();
#else
    std::error_code ec;
    if (!std::filesystem::exists(basePath(), ec)) {
        return true;
    }

    for (const auto& entry : std::filesystem::directory_iterator(basePath(), ec)) {
        if (ec) {
            return false;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        std::uint32_t sequence = 0;
        if (parseRecordSequenceFromName(name.c_str(), sequence)) {
            out.push_back(sequence);
        }
    }
#endif

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

bool recoverIndexFromRecordFiles(Index& out) {
    std::vector<std::uint32_t> sequences;
    if (!listRecordSequences(sequences)) {
        return false;
    }

    out = Index{};
    if (sequences.empty()) {
        return true;
    }

    const std::set<std::uint32_t> existing(sequences.begin(), sequences.end());
    const std::uint32_t head = sequences.front();

    std::uint32_t count = 0;
    std::uint32_t sequence = head;
    for (;;) {
        if (!recordFileExists(sequence, existing)) {
            break;
        }

        api::Record request;
        if (!readRecord(sequence, request)) {
            break;
        }

        ++count;
        if (sequence == 0xFFFFFFFFu) {
            break;
        }
        ++sequence;
    }

    out.head = head;
    out.tail = head + count;
    out.count = count;
    return true;
}

bool indexContains(const Index& index, std::uint32_t sequence) {
    return index.count > 0 && sequence >= index.head && sequence < index.tail;
}

bool sanitizeIndexedRange(const Index& input, Index& out) {
    out = Index{};

    std::uint32_t scanned = 0;
    for (std::uint32_t sequence = input.head;
         sequence < input.tail && scanned < input.count;
         ++sequence, ++scanned) {
        api::Record request;
        if (!readRecord(sequence, request)) {
            if (out.count == 0) {
                continue;
            }
            break;
        }

        if (out.count == 0) {
            out.head = sequence;
            out.tail = sequence;
        }

        out.tail = sequence + 1;
        out.count += 1;
    }

    if (out.count == 0) {
        out.head = input.tail;
        out.tail = input.tail;
    }

    return true;
}

void deleteRecordsOutsideLiveIndex(const Index& live) {
    std::vector<std::uint32_t> sequences;
    if (!listRecordSequences(sequences)) {
        return;
    }

    for (std::uint32_t sequence : sequences) {
        if (!indexContains(live, sequence)) {
            removeRecord(sequence);
        }
    }
}
bool sameIndex(const Index& a, const Index& b) {
    return a.head == b.head && a.tail == b.tail && a.count == b.count;
}

bool readBestIndexRecord(IndexRecord& out, bool& fromA) {
    IndexRecord a;
    IndexRecord b;

#ifdef ARDUINO
    const bool hasA = readIndexRecord(kIndexAPath, a);
    const bool hasB = readIndexRecord(kIndexBPath, b);
#else
    const std::string indexAPath = storePath(kIndexAFileName);
    const std::string indexBPath = storePath(kIndexBFileName);
    const bool hasA = readIndexRecord(indexAPath.c_str(), a);
    const bool hasB = readIndexRecord(indexBPath.c_str(), b);
#endif

    if (!hasA && !hasB) {
        return false;
    }

    if (hasA && (!hasB || a.version >= b.version)) {
        out = a;
        fromA = true;
        return true;
    }

    out = b;
    fromA = false;
    return true;
}

} // namespace

bool mount() {
#ifdef ARDUINO
    if (mounted) {
        return true;
    }

    mounted = LittleFS.begin(false);
    if (!mounted) {
        logLine(LogLevel::Warn, "Record file store mount failed");
    }

    return mounted;
#else
    std::error_code ec;
    std::filesystem::create_directories(basePath(), ec);
    if (ec) {
        logLine(LogLevel::Warn, "Record file store directory create failed");
        return false;
    }
    return true;
#endif
}

void setBasePath(const char* path) {
#ifdef ARDUINO
    (void)path;
#else
    std::string next = (path == nullptr || path[0] == '\0') ? std::string("spool") : std::string(path);
    while (next.size() > 1 && next.back() == '/') {
        next.pop_back();
    }
    basePath() = next;
#endif
}

bool readIndex(Index& out) {
    if (!mount()) {
        return false;
    }

    IndexRecord record;
    bool fromA = true;
    if (!readBestIndexRecord(record, fromA)) {
        if (!recoverIndexFromRecordFiles(out)) {
            return false;
        }
        deleteRecordsOutsideLiveIndex(out);
        if (out.count > 0) {
            writeIndex(out);
        }
        return true;
    }

    Index fromIndex;
    fromIndex.head = record.head;
    fromIndex.tail = record.tail;
    fromIndex.count = record.count;

    if (!sanitizeIndexedRange(fromIndex, out)) {
        return false;
    }

    deleteRecordsOutsideLiveIndex(out);

    if (!sameIndex(fromIndex, out)) {
        writeIndex(out);
    }

    return true;
}
bool writeIndex(const Index& index) {
    if (!mount()) {
        return false;
    }

    IndexRecord previous;
    bool previousFromA = true;
    const bool hasPrevious = readBestIndexRecord(previous, previousFromA);

    IndexRecord next;
    next.version = hasPrevious ? previous.version + 1 : 1;
    next.head = index.head;
    next.tail = index.tail;
    next.count = index.count;
    next.crc = indexCrc(next);

#ifdef ARDUINO
    const char* targetPath = hasPrevious && previousFromA ? kIndexBPath : kIndexAPath;
    if (!writeIndexRecord(targetPath, next)) {
#else
    const std::string targetPath = hasPrevious && previousFromA
        ? storePath(kIndexBFileName)
        : storePath(kIndexAFileName);
    if (!writeIndexRecord(targetPath.c_str(), next)) {
#endif
        logLine(LogLevel::Warn, "Record file store index write failed");
        return false;
    }

    return true;
}

bool writeRecord(std::uint32_t sequence, const api::Record& request) {
    if (!mount()) {
        return false;
    }

    if (request.path.empty() ||
        request.path.size() > kMaxPathBytes ||
        request.payload.size() > kMaxPayloadBytes) {
        return false;
    }

    char path[kMaxFilePathBytes];
    if (!makeRecordPath(sequence, path, sizeof(path))) {
        return false;
    }

    RecordHeader header;
    header.pathBytes = static_cast<std::uint16_t>(request.path.size());
    header.payloadBytes = static_cast<std::uint32_t>(request.payload.size());
    header.timeoutRetryCount = request.timeoutRetryCount;
    header.tlsRetryCount = request.tlsRetryCount;
    header.crc = recordCrc(header, request.path.data(), request.payload.data());

#ifdef ARDUINO
    File file = LittleFS.open(path, "w");
    if (!file) {
        logLine(LogLevel::Warn, "Record file store record write failed");
        return false;
    }

    const bool ok =
        writeAll(file, &header, sizeof(header)) &&
        writeAll(file, request.path.data(), request.path.size()) &&
        writeAll(file, request.payload.data(), request.payload.size());

    file.close();
    return ok;
#else
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        logLine(LogLevel::Warn, "Record file store record write failed");
        return false;
    }

    return writeAll(file, &header, sizeof(header)) &&
           writeAll(file, request.path.data(), request.path.size()) &&
           writeAll(file, request.payload.data(), request.payload.size());
#endif
}

bool readRecord(std::uint32_t sequence, api::Record& out) {
    if (!mount()) {
        return false;
    }

    char path[kMaxFilePathBytes];
    if (!makeRecordPath(sequence, path, sizeof(path))) {
        return false;
    }

    RecordHeader header;

#ifdef ARDUINO
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    if (!readAll(file, &header, sizeof(header))) {
        file.close();
        return false;
    }
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    if (!readAll(file, &header, sizeof(header))) {
        return false;
    }
#endif

    if (header.magic != kRecordMagic ||
        header.formatVersion != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.pathBytes == 0 ||
        header.pathBytes > kMaxPathBytes ||
        header.payloadBytes > kMaxPayloadBytes) {
#ifdef ARDUINO
        file.close();
#endif
        return false;
    }

    char recordPath[kMaxPathBytes + 1]{};
    std::string payload;
    payload.resize(header.payloadBytes);

#ifdef ARDUINO
    const bool ok =
        readAll(file, recordPath, header.pathBytes) &&
        readAll(file, payload.data(), payload.size());
    file.close();

    if (!ok) {
        return false;
    }
#else
    if (!readAll(file, recordPath, header.pathBytes) ||
        !readAll(file, payload.data(), payload.size())) {
        return false;
    }
#endif

    if (header.crc != recordCrc(header, recordPath, payload.data())) {
        return false;
    }

    recordPath[header.pathBytes] = '\0';

    out.path = recordPath;
    out.payload = payload;
    out.timeoutRetryCount = header.timeoutRetryCount;
    out.tlsRetryCount = header.tlsRetryCount;
    return true;
}

bool removeRecord(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }

    char path[kMaxFilePathBytes];
    if (!makeRecordPath(sequence, path, sizeof(path))) {
        return false;
    }

#ifdef ARDUINO
    return LittleFS.remove(path);
#else
    std::error_code ec;
    return std::filesystem::remove(path, ec) || !std::filesystem::exists(path, ec);
#endif
}

std::uint64_t freeBytes() {
    if (!mount()) {
        return 0;
    }

#ifdef ARDUINO
    return LittleFS.totalBytes() - LittleFS.usedBytes();
#else
    std::error_code ec;
    const auto info = std::filesystem::space(basePath(), ec);
    if (ec) {
        return 0;
    }

    return static_cast<std::uint64_t>(info.available);
#endif
}

namespace {

class FileRecordStore final : public api::RecordStore {
public:
    bool readIndex(api::RecordStoreIndex& out) override {
        Index index;
        if (!api::record_file_store::readIndex(index)) {
            return false;
        }
        out.head = index.head;
        out.tail = index.tail;
        out.count = index.count;
        return true;
    }

    bool writeIndex(const api::RecordStoreIndex& index) override {
        Index fileIndex;
        fileIndex.head = index.head;
        fileIndex.tail = index.tail;
        fileIndex.count = index.count;
        return api::record_file_store::writeIndex(fileIndex);
    }

    bool writeRecord(std::uint32_t sequence, const api::Record& request) override {
        return api::record_file_store::writeRecord(sequence, request);
    }

    bool readRecord(std::uint32_t sequence, api::Record& out) override {
        return api::record_file_store::readRecord(sequence, out);
    }

    bool removeRecord(std::uint32_t sequence) override {
        return api::record_file_store::removeRecord(sequence);
    }

    std::uint64_t freeBytes() override {
        return api::record_file_store::freeBytes();
    }
};

} // namespace

RecordStore& defaultStore() {
    static FileRecordStore store;
    return store;
}

} // namespace api::record_file_store
