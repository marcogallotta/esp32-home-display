#include "request_file_store.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../log.h"

#ifdef ARDUINO
#include <LittleFS.h>
#else
#include <filesystem>
#include <fstream>
#endif

namespace api::request_file_store {
namespace {

constexpr std::uint32_t kIndexMagic = 0x41504949;   // APII
constexpr std::uint32_t kRequestMagic = 0x41504952; // APIR
constexpr std::uint16_t kFormatVersion = 1;

constexpr std::size_t kMaxPathBytes = 96;
constexpr std::size_t kMaxBodyBytes = 4096;

#ifdef ARDUINO
constexpr const char* kIndexAPath = "/api_idx_a.bin";
constexpr const char* kIndexBPath = "/api_idx_b.bin";
#else
constexpr const char* kIndexAPath = "api_idx_a.bin";
constexpr const char* kIndexBPath = "api_idx_b.bin";
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

struct RequestHeader {
    std::uint32_t magic = kRequestMagic;
    std::uint16_t formatVersion = kFormatVersion;
    std::uint16_t headerBytes = sizeof(RequestHeader);
    std::uint16_t pathBytes = 0;
    std::uint16_t reserved = 0;
    std::uint32_t bodyBytes = 0;
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

std::uint32_t requestCrc(
    const RequestHeader& header,
    const char* path,
    const char* body
) {
    std::uint32_t crc = crc32Update(0, &header, offsetof(RequestHeader, crc));
    crc = crc32Update(crc, path, header.pathBytes);
    crc = crc32Update(crc, body, header.bodyBytes);
    return crc;
}

bool makeRequestPath(std::uint32_t sequence, char* out, std::size_t outSize) {
#ifdef ARDUINO
    const int n = std::snprintf(out, outSize, "/api_req_%08lu.bin", static_cast<unsigned long>(sequence));
#else
    const int n = std::snprintf(out, outSize, "api_req_%08lu.bin", static_cast<unsigned long>(sequence));
#endif
    return n > 0 && static_cast<std::size_t>(n) < outSize;
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

bool readBestIndexRecord(IndexRecord& out, bool& fromA) {
    IndexRecord a;
    IndexRecord b;

    const bool hasA = readIndexRecord(kIndexAPath, a);
    const bool hasB = readIndexRecord(kIndexBPath, b);

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
        logLine(LogLevel::Warn, "Request file store mount failed");
    }

    return mounted;
#else
    return true;
#endif
}

bool readIndex(Index& out) {
    if (!mount()) {
        return false;
    }

    IndexRecord record;
    bool fromA = true;

    if (!readBestIndexRecord(record, fromA)) {
        out = Index{};
        return true;
    }

    out.head = record.head;
    out.tail = record.tail;
    out.count = record.count;
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

    const char* targetPath = hasPrevious && previousFromA ? kIndexBPath : kIndexAPath;

    if (!writeIndexRecord(targetPath, next)) {
        logLine(LogLevel::Warn, "Request file store index write failed");
        return false;
    }

    return true;
}

bool writeRequest(std::uint32_t sequence, const BufferedRequest& request) {
    if (!mount()) {
        return false;
    }

    if (request.path.empty() ||
        request.path.size() > kMaxPathBytes ||
        request.body.size() > kMaxBodyBytes) {
        return false;
    }

    char path[32];
    if (!makeRequestPath(sequence, path, sizeof(path))) {
        return false;
    }

    RequestHeader header;
    header.pathBytes = static_cast<std::uint16_t>(request.path.size());
    header.bodyBytes = static_cast<std::uint32_t>(request.body.size());
    header.timeoutRetryCount = request.timeoutRetryCount;
    header.tlsRetryCount = request.tlsRetryCount;
    header.crc = requestCrc(header, request.path.data(), request.body.data());

#ifdef ARDUINO
    File file = LittleFS.open(path, "w");
    if (!file) {
        logLine(LogLevel::Warn, "Request file store request write failed");
        return false;
    }

    const bool ok =
        writeAll(file, &header, sizeof(header)) &&
        writeAll(file, request.path.data(), request.path.size()) &&
        writeAll(file, request.body.data(), request.body.size());

    file.close();
    return ok;
#else
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        logLine(LogLevel::Warn, "Request file store request write failed");
        return false;
    }

    return writeAll(file, &header, sizeof(header)) &&
           writeAll(file, request.path.data(), request.path.size()) &&
           writeAll(file, request.body.data(), request.body.size());
#endif
}

bool readRequest(std::uint32_t sequence, BufferedRequest& out) {
    if (!mount()) {
        return false;
    }

    char path[32];
    if (!makeRequestPath(sequence, path, sizeof(path))) {
        return false;
    }

    RequestHeader header;

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

    if (header.magic != kRequestMagic ||
        header.formatVersion != kFormatVersion ||
        header.headerBytes != sizeof(RequestHeader) ||
        header.pathBytes == 0 ||
        header.pathBytes > kMaxPathBytes ||
        header.bodyBytes > kMaxBodyBytes) {
#ifdef ARDUINO
        file.close();
#endif
        return false;
    }

    char requestPath[kMaxPathBytes + 1]{};
    std::string body;
    body.resize(header.bodyBytes);

#ifdef ARDUINO
    const bool ok =
        readAll(file, requestPath, header.pathBytes) &&
        readAll(file, body.data(), body.size());
    file.close();

    if (!ok) {
        return false;
    }
#else
    if (!readAll(file, requestPath, header.pathBytes) ||
        !readAll(file, body.data(), body.size())) {
        return false;
    }
#endif

    if (header.crc != requestCrc(header, requestPath, body.data())) {
        return false;
    }

    requestPath[header.pathBytes] = '\0';

    out.path = requestPath;
    out.body = body;
    out.timeoutRetryCount = header.timeoutRetryCount;
    out.tlsRetryCount = header.tlsRetryCount;
    return true;
}

bool removeRequest(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }

    char path[32];
    if (!makeRequestPath(sequence, path, sizeof(path))) {
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
    const auto info = std::filesystem::space(".", ec);
    if (ec) {
        return 0;
    }

    return static_cast<std::uint64_t>(info.available);
#endif
}

namespace {

class FileRequestStore final : public api::RequestStore {
public:
    bool readIndex(api::RequestStoreIndex& out) override {
        Index index;
        if (!api::request_file_store::readIndex(index)) {
            return false;
        }
        out.head = index.head;
        out.tail = index.tail;
        out.count = index.count;
        return true;
    }

    bool writeIndex(const api::RequestStoreIndex& index) override {
        Index fileIndex;
        fileIndex.head = index.head;
        fileIndex.tail = index.tail;
        fileIndex.count = index.count;
        return api::request_file_store::writeIndex(fileIndex);
    }

    bool writeRequest(std::uint32_t sequence, const BufferedRequest& request) override {
        return api::request_file_store::writeRequest(sequence, request);
    }

    bool readRequest(std::uint32_t sequence, BufferedRequest& out) override {
        return api::request_file_store::readRequest(sequence, out);
    }

    bool removeRequest(std::uint32_t sequence) override {
        return api::request_file_store::removeRequest(sequence);
    }

    std::uint64_t freeBytes() override {
        return api::request_file_store::freeBytes();
    }
};

} // namespace

RequestStore& defaultStore() {
    static FileRequestStore store;
    return store;
}

} // namespace api::request_file_store
