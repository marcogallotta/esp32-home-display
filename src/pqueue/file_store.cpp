#include "file_store.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef ARDUINO
#include <filesystem>
#else
#include <LittleFS.h>
#endif

namespace pqueue {
namespace {

constexpr std::uint32_t kIndexMagic = 0x50514958;  // PQIX
constexpr std::uint32_t kRecordMagic = 0x50515243; // PQRC
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kMaxPathBytes = 160;
constexpr std::size_t kMaxRecordBytes = 4096;

struct IndexRecord {
    std::uint32_t magic = kIndexMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t reserved = 0;
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
    std::uint32_t generation = 0;
    std::uint32_t crc = 0;
};

struct RecordHeader {
    std::uint32_t magic = kRecordMagic;
    std::uint16_t version = kFormatVersion;
    std::uint16_t headerBytes = sizeof(RecordHeader);
    std::uint32_t recordBytes = 0;
    std::uint32_t crc = 0;
};

std::uint32_t crc32Update(std::uint32_t crc, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

std::uint32_t indexCrc(const IndexRecord& record) {
    return crc32Update(0, &record, offsetof(IndexRecord, crc));
}

std::uint32_t recordCrc(const RecordHeader& header, const std::string& record) {
    std::uint32_t crc = crc32Update(0, &header, offsetof(RecordHeader, crc));
    return crc32Update(crc, record.data(), record.size());
}

bool validIndex(const IndexRecord& record) {
    return record.magic == kIndexMagic &&
           record.version == kFormatVersion &&
           record.crc == indexCrc(record) &&
           record.tail >= record.head &&
           record.count == record.tail - record.head;
}

IndexRecord toRecord(const FileStoreIndex& index, std::uint32_t generation) {
    IndexRecord out;
    out.head = index.head;
    out.tail = index.tail;
    out.count = index.count;
    out.generation = generation;
    out.crc = indexCrc(out);
    return out;
}

FileStoreIndex fromRecord(const IndexRecord& record) {
    FileStoreIndex out;
    out.head = record.head;
    out.tail = record.tail;
    out.count = record.count;
    return out;
}

#ifndef ARDUINO
std::string joinPath(const std::string& base, const char* name) {
    return (std::filesystem::path(base) / name).string();
}
#endif

bool makeRecordName(std::uint32_t sequence, char* out, std::size_t outSize) {
    const int n = std::snprintf(out, outSize, "pqueue_rec_%08lu.bin", static_cast<unsigned long>(sequence));
    return n > 0 && static_cast<std::size_t>(n) < outSize;
}

#ifndef ARDUINO
std::string recordPath(const std::string& base, std::uint32_t sequence) {
    char name[kMaxPathBytes];
    if (!makeRecordName(sequence, name, sizeof(name))) {
        return {};
    }
    return joinPath(base, name);
}

std::string indexPath(const std::string& base, bool a) {
    return joinPath(base, a ? "pqueue_idx_a.bin" : "pqueue_idx_b.bin");
}
#endif

bool parseRecordSequence(const std::string& name, std::uint32_t& out) {
    unsigned long value = 0;
    char suffix = '\0';
    if (std::sscanf(name.c_str(), "pqueue_rec_%08lu.bin%c", &value, &suffix) != 1) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

#ifndef ARDUINO
bool readIndexAt(const std::string& path, IndexRecord& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.gcount() == static_cast<std::streamsize>(sizeof(out)) && validIndex(out);
}

bool writeIndexAt(const std::string& path, const IndexRecord& record) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(&record), sizeof(record));
    return file.good();
}
#endif

} // namespace

FileStore::FileStore(std::string basePath) : basePath_(std::move(basePath)) {}

bool FileStore::mount() {
#ifndef ARDUINO
    std::error_code ec;
    std::filesystem::create_directories(basePath_, ec);
    return !ec;
#else
    return LittleFS.begin(false);
#endif
}

bool FileStore::readIndex(FileStoreIndex& out) {
    if (!mount()) {
        return false;
    }

#ifndef ARDUINO
    IndexRecord a;
    IndexRecord b;
    const bool hasA = readIndexAt(indexPath(basePath_, true), a);
    const bool hasB = readIndexAt(indexPath(basePath_, false), b);

    if (hasA && hasB) {
        out = fromRecord(a.generation >= b.generation ? a : b);
        return true;
    }
    if (hasA) {
        out = fromRecord(a);
        return true;
    }
    if (hasB) {
        out = fromRecord(b);
        return true;
    }

    std::vector<std::uint32_t> sequences;
    for (const auto& entry : std::filesystem::directory_iterator(basePath_)) {
        std::uint32_t sequence = 0;
        if (parseRecordSequence(entry.path().filename().string(), sequence)) {
            sequences.push_back(sequence);
        }
    }
    if (sequences.empty()) {
        out = {};
        return true;
    }
    std::sort(sequences.begin(), sequences.end());
    out.head = sequences.front();
    out.tail = sequences.back() + 1;
    out.count = out.tail - out.head;
    writeIndex(out);
    return true;
#else
    out = {};
    return true;
#endif
}

bool FileStore::writeIndex(const FileStoreIndex& index) {
    if (!mount()) {
        return false;
    }
#ifndef ARDUINO
    IndexRecord currentA;
    IndexRecord currentB;
    const bool hasA = readIndexAt(indexPath(basePath_, true), currentA);
    const bool hasB = readIndexAt(indexPath(basePath_, false), currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.generation : 0, hasB ? currentB.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.generation <= currentB.generation);
    return writeIndexAt(indexPath(basePath_, writeA), toRecord(index, generation));
#else
    (void)index;
    return false;
#endif
}

bool FileStore::writeRecord(std::uint32_t sequence, const std::string& record) {
    if (!mount() || record.size() > kMaxRecordBytes) {
        return false;
    }
    RecordHeader header;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);
#ifndef ARDUINO
    std::ofstream file(recordPath(basePath_, sequence), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(record.data(), static_cast<std::streamsize>(record.size()));
    return file.good();
#else
    (void)sequence;
    return false;
#endif
}

bool FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    if (!mount()) {
        return false;
    }
#ifndef ARDUINO
    std::ifstream file(recordPath(basePath_, sequence), std::ios::binary);
    if (!file) {
        return false;
    }
    RecordHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header)) ||
        header.magic != kRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.recordBytes > kMaxRecordBytes) {
        return false;
    }
    std::string record(header.recordBytes, '\0');
    file.read(record.data(), static_cast<std::streamsize>(record.size()));
    if (file.gcount() != static_cast<std::streamsize>(record.size()) ||
        header.crc != recordCrc(header, record)) {
        return false;
    }
    out = std::move(record);
    return true;
#else
    (void)sequence;
    return false;
#endif
}

bool FileStore::removeRecord(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }
#ifndef ARDUINO
    std::error_code ec;
    std::filesystem::remove(recordPath(basePath_, sequence), ec);
    return !ec;
#else
    (void)sequence;
    return false;
#endif
}

std::uint64_t FileStore::freeBytes() const {
#ifndef ARDUINO
    std::error_code ec;
    const auto info = std::filesystem::space(basePath_, ec);
    return ec ? 0 : static_cast<std::uint64_t>(info.available);
#else
    return 0;
#endif
}

} // namespace pqueue
