#include "file_store.h"
#include "storage_common.h"

#ifdef ARDUINO

#include <LittleFS.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace pqueue {
namespace {

using namespace storage_detail;

std::string normalizeBasePath(const std::string& basePath) {
    if (basePath.empty() || basePath == "/") {
        return "/";
    }
    std::string out = basePath.front() == '/' ? basePath : "/" + basePath;
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::string joinPath(const std::string& base, const char* name) {
    if (base == "/") {
        return std::string("/") + name;
    }
    return base + "/" + name;
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

std::string baseName(const char* path) {
    if (path == nullptr) {
        return {};
    }
    const char* slash = std::strrchr(path, '/');
    return slash == nullptr ? std::string(path) : std::string(slash + 1);
}

bool ensureDirectory(const std::string& path) {
    if (path == "/" || LittleFS.exists(path.c_str())) {
        return true;
    }
    return LittleFS.mkdir(path.c_str());
}

bool readIndexAt(const std::string& path, IndexRecord& out) {
    File file = LittleFS.open(path.c_str(), "r");
    if (!file) {
        return false;
    }
    const std::size_t bytesRead = file.read(reinterpret_cast<std::uint8_t*>(&out), sizeof(out));
    file.close();
    return bytesRead == sizeof(out) && validIndex(out);
}

bool writeIndexAt(const std::string& path, const IndexRecord& record) {
    const std::string tempPath = path + ".tmp";
    {
        File file = LittleFS.open(tempPath.c_str(), "w");
        if (!file) {
            return false;
        }
        const std::size_t bytesWritten = file.write(reinterpret_cast<const std::uint8_t*>(&record), sizeof(record));
        file.flush();
        file.close();
        if (bytesWritten != sizeof(record)) {
            LittleFS.remove(tempPath.c_str());
            return false;
        }
    }

    LittleFS.remove(path.c_str());
    return LittleFS.rename(tempPath.c_str(), path.c_str());
}

} // namespace

bool FileStore::mount() {
    if (resolvedBackend() != StorageBackend::LittleFS) {
        // TODO: expose storage backend errors through the library logging/error API.
        return false;
    }

    // Never format automatically. Users must make that decision explicitly outside PQUEUE.
    if (!LittleFS.begin(false)) {
        return false;
    }

    return ensureDirectory(normalizeBasePath(config_.basePath));
}

bool FileStore::readIndex(FileStoreIndex& out) {
    if (!mount()) {
        return false;
    }

    const std::string base = normalizeBasePath(config_.basePath);
    IndexRecord a;
    IndexRecord b;
    const bool hasA = readIndexAt(indexPath(base, true), a);
    const bool hasB = readIndexAt(indexPath(base, false), b);

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
    File dir = LittleFS.open(base.c_str());
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            std::uint32_t sequence = 0;
            if (!file.isDirectory() && parseRecordSequence(baseName(file.name()), sequence)) {
                sequences.push_back(sequence);
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }

    out = rebuildIndexFromSequences(sequences);
    writeIndex(out);
    return true;
}

bool FileStore::writeIndex(const FileStoreIndex& index) {
    if (!mount()) {
        return false;
    }

    const std::string base = normalizeBasePath(config_.basePath);
    IndexRecord currentA;
    IndexRecord currentB;
    const bool hasA = readIndexAt(indexPath(base, true), currentA);
    const bool hasB = readIndexAt(indexPath(base, false), currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.generation : 0, hasB ? currentB.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.generation <= currentB.generation);
    return writeIndexAt(indexPath(base, writeA), toRecord(index, generation));
}

bool FileStore::writeRecord(std::uint32_t sequence, const std::string& record) {
    if (!mount() || record.size() > kMaxRecordBytes) {
        return false;
    }

    const std::string base = normalizeBasePath(config_.basePath);
    const std::string path = recordPath(base, sequence);
    const std::string tempPath = path + ".tmp";

    RecordHeader header;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);

    {
        File file = LittleFS.open(tempPath.c_str(), "w");
        if (!file) {
            return false;
        }
        const std::size_t headerBytes = file.write(reinterpret_cast<const std::uint8_t*>(&header), sizeof(header));
        const std::size_t recordBytes = file.write(reinterpret_cast<const std::uint8_t*>(record.data()), record.size());
        file.flush();
        file.close();
        if (headerBytes != sizeof(header) || recordBytes != record.size()) {
            LittleFS.remove(tempPath.c_str());
            return false;
        }
    }

    LittleFS.remove(path.c_str());
    return LittleFS.rename(tempPath.c_str(), path.c_str());
}

bool FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    if (!mount()) {
        return false;
    }

    const std::string path = recordPath(normalizeBasePath(config_.basePath), sequence);
    File file = LittleFS.open(path.c_str(), "r");
    if (!file) {
        return false;
    }

    RecordHeader header;
    const std::size_t headerBytes = file.read(reinterpret_cast<std::uint8_t*>(&header), sizeof(header));
    if (headerBytes != sizeof(header) ||
        header.magic != kFileStoreRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.recordBytes > kMaxRecordBytes) {
        file.close();
        return false;
    }

    std::string record(header.recordBytes, '\0');
    const std::size_t recordBytes = file.read(reinterpret_cast<std::uint8_t*>(&record[0]), record.size());
    file.close();
    if (recordBytes != record.size() || header.crc != recordCrc(header, record)) {
        return false;
    }

    out = std::move(record);
    return true;
}

bool FileStore::removeRecord(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }
    return LittleFS.remove(recordPath(normalizeBasePath(config_.basePath), sequence).c_str());
}

std::uint64_t FileStore::freeBytes() const {
    if (resolvedBackend() != StorageBackend::LittleFS) {
        return 0;
    }
    const auto total = LittleFS.totalBytes();
    const auto used = LittleFS.usedBytes();
    return used <= total ? static_cast<std::uint64_t>(total - used) : 0;
}

} // namespace pqueue

#endif // ARDUINO
