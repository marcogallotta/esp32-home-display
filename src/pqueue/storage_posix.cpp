#include "file_store.h"
#include "storage_common.h"

#ifndef ARDUINO

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace pqueue {
namespace {

using namespace storage_detail;

std::string joinPath(const std::string& base, const char* name) {
    return (std::filesystem::path(base) / name).string();
}

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

bool readIndexAt(const std::string& path, IndexRecord& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.read(reinterpret_cast<char*>(&out), sizeof(out));
    return file.gcount() == static_cast<std::streamsize>(sizeof(out)) && validIndex(out);
}

bool writeIndexAt(const std::string& path, const IndexRecord& record) {
    const std::string tempPath = path + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(&record), sizeof(record));
        if (!file.good()) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    return !ec;
}

} // namespace

bool FileStore::mount() {
    if (resolvedBackend() != StorageBackend::Posix) {
        // TODO: expose storage backend errors through the library logging/error API.
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(config_.basePath, ec);
    return !ec;
}

bool FileStore::readIndex(FileStoreIndex& out) {
    if (!mount()) {
        return false;
    }

    IndexRecord a;
    IndexRecord b;
    const bool hasA = readIndexAt(indexPath(config_.basePath, true), a);
    const bool hasB = readIndexAt(indexPath(config_.basePath, false), b);

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
    for (const auto& entry : std::filesystem::directory_iterator(config_.basePath)) {
        std::uint32_t sequence = 0;
        if (parseRecordSequence(entry.path().filename().string(), sequence)) {
            sequences.push_back(sequence);
        }
    }

    out = rebuildIndexFromSequences(sequences);
    writeIndex(out);
    return true;
}

bool FileStore::writeIndex(const FileStoreIndex& index) {
    if (!mount()) {
        return false;
    }

    IndexRecord currentA;
    IndexRecord currentB;
    const bool hasA = readIndexAt(indexPath(config_.basePath, true), currentA);
    const bool hasB = readIndexAt(indexPath(config_.basePath, false), currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.generation : 0, hasB ? currentB.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.generation <= currentB.generation);
    return writeIndexAt(indexPath(config_.basePath, writeA), toRecord(index, generation));
}

bool FileStore::writeRecord(std::uint32_t sequence, const std::string& record) {
    if (!mount() || record.size() > kMaxRecordBytes) {
        return false;
    }

    const std::string path = recordPath(config_.basePath, sequence);
    const std::string tempPath = path + ".tmp";

    RecordHeader header;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);

    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(record.data(), static_cast<std::streamsize>(record.size()));
        if (!file.good()) {
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
    return !ec;
}

bool FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    if (!mount()) {
        return false;
    }

    std::ifstream file(recordPath(config_.basePath, sequence), std::ios::binary);
    if (!file) {
        return false;
    }

    RecordHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header)) ||
        header.magic != kFileStoreRecordMagic ||
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
}

bool FileStore::removeRecord(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(recordPath(config_.basePath, sequence), ec);
    return !ec;
}

std::uint64_t FileStore::freeBytes() const {
    if (resolvedBackend() != StorageBackend::Posix) {
        return 0;
    }
    std::error_code ec;
    const auto info = std::filesystem::space(config_.basePath, ec);
    return ec ? 0 : static_cast<std::uint64_t>(info.available);
}

} // namespace pqueue

#endif // !ARDUINO
