#include "file_store.h"
#include "storage_common.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace pqueue {
namespace {

using namespace storage_detail;

constexpr const char* kIndexAName = "pqueue_idx_a.bin";
constexpr const char* kIndexBName = "pqueue_idx_b.bin";

std::string bytesFromObject(const void* data, std::size_t size) {
    return std::string(static_cast<const char*>(data), size);
}

bool readIndexAt(FileSystem& fs, const char* name, IndexRecord& out) {
    std::string bytes;
    if (!fs.readFile(name, bytes) || bytes.size() != sizeof(out)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(out));
    return validIndex(out);
}

bool fileExists(FileSystem& fs, const std::string& name) {
    std::string ignored;
    return fs.readFile(name, ignored);
}

bool writeFileAtomic(FileSystem& fs, const std::string& name, const std::string& bytes) {
    const std::string tempName = name + ".tmp";
    const std::string backupName = name + ".bak";

    if (!fs.writeFile(tempName, bytes)) {
        return false;
    }

    if (!fileExists(fs, name)) {
        if (fs.renameFile(tempName, name)) {
            return true;
        }
        fs.removeFile(tempName);
        return false;
    }

    fs.removeFile(backupName);
    if (!fs.renameFile(name, backupName)) {
        fs.removeFile(tempName);
        return false;
    }

    if (fs.renameFile(tempName, name)) {
        fs.removeFile(backupName);
        return true;
    }

    fs.renameFile(backupName, name);
    fs.removeFile(tempName);
    return false;
}

bool writeIndexAt(FileSystem& fs, const char* name, const IndexRecord& record) {
    return writeFileAtomic(fs, name, bytesFromObject(&record, sizeof(record)));
}

std::string recordName(std::uint32_t sequence) {
    char name[kMaxPathBytes];
    if (!makeRecordName(sequence, name, sizeof(name))) {
        return {};
    }
    return name;
}

} // namespace

FileStore::FileStore(FileStoreConfig config)
    : config_(std::move(config)), fileSystem_(config_.fileSystem) {}

FileStore::FileStore(std::string basePath) {
    config_.basePath = std::move(basePath);
}

StorageBackend FileStore::resolvedBackend() const {
    if (config_.backend != StorageBackend::Default) {
        return config_.backend;
    }
#ifdef ARDUINO
    return StorageBackend::LittleFS;
#else
    return StorageBackend::Posix;
#endif
}

std::shared_ptr<FileSystem> FileStore::fileSystem() const {
    if (fileSystem_) {
        return fileSystem_;
    }

    switch (resolvedBackend()) {
    case StorageBackend::Posix:
        fileSystem_ = makePosixFileSystem();
        break;
    case StorageBackend::LittleFS:
        fileSystem_ = makeLittleFsFileSystem();
        break;
    case StorageBackend::Default:
        break;
    }

    return fileSystem_;
}

bool FileStore::mount() {
    const auto fs = fileSystem();
    return fs && fs->mount(config_.basePath);
}

bool FileStore::readIndex(FileStoreIndex& out) {
    if (!mount()) {
        return false;
    }
    const auto fs = fileSystem();

    IndexRecord a;
    IndexRecord b;
    const bool hasA = readIndexAt(*fs, kIndexAName, a);
    const bool hasB = readIndexAt(*fs, kIndexBName, b);

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

    std::vector<std::string> files;
    if (!fs->listFiles(files)) {
        return false;
    }

    std::vector<std::uint32_t> sequences;
    for (const auto& name : files) {
        std::uint32_t sequence = 0;
        if (parseRecordSequence(name, sequence)) {
            sequences.push_back(sequence);
        }
    }

    out = rebuildIndexFromSequences(sequences);
    return writeIndex(out);
}

bool FileStore::writeIndex(const FileStoreIndex& index) {
    if (!mount()) {
        return false;
    }
    const auto fs = fileSystem();

    IndexRecord currentA;
    IndexRecord currentB;
    const bool hasA = readIndexAt(*fs, kIndexAName, currentA);
    const bool hasB = readIndexAt(*fs, kIndexBName, currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.generation : 0, hasB ? currentB.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.generation <= currentB.generation);
    return writeIndexAt(*fs, writeA ? kIndexAName : kIndexBName, toRecord(index, generation));
}

bool FileStore::writeRecord(std::uint32_t sequence, const std::string& record) {
    if (!mount() || record.size() > kMaxRecordBytes) {
        return false;
    }

    const std::string name = recordName(sequence);
    if (name.empty()) {
        return false;
    }
    RecordHeader header;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);

    std::string bytes = bytesFromObject(&header, sizeof(header));
    bytes.append(record);
    return writeFileAtomic(*fileSystem(), name, bytes);
}

bool FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    if (!mount()) {
        return false;
    }

    const std::string name = recordName(sequence);
    std::string bytes;
    if (name.empty() || !fileSystem()->readFile(name, bytes) || bytes.size() < sizeof(RecordHeader)) {
        return false;
    }

    RecordHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kFileStoreRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.recordBytes > kMaxRecordBytes) {
        return false;
    }

    if (bytes.size() != sizeof(RecordHeader) + header.recordBytes) {
        return false;
    }

    std::string record = bytes.substr(sizeof(RecordHeader));
    if (header.crc != recordCrc(header, record)) {
        return false;
    }

    out = std::move(record);
    return true;
}

bool FileStore::removeRecord(std::uint32_t sequence) {
    if (!mount()) {
        return false;
    }

    const std::string name = recordName(sequence);
    return !name.empty() && fileSystem()->removeFile(name);
}

std::uint64_t FileStore::freeBytes() const {
    const auto fs = fileSystem();
    if (!fs || !fs->mount(config_.basePath)) {
        return 0;
    }
    return fs->freeBytes();
}

} // namespace pqueue
