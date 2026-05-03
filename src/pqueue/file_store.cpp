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
    const Status read = fs.readFile(name, bytes);
    if (!read.ok() || bytes.size() != sizeof(out)) {
        return false;
    }
    std::memcpy(&out, bytes.data(), sizeof(out));
    return validIndex(out);
}

bool fileExists(FileSystem& fs, const std::string& name) {
    std::string ignored;
    return fs.readFile(name, ignored).ok();
}

Status writeFileAtomic(FileSystem& fs, const std::string& name, const std::string& bytes) {
    const std::string tempName = name + ".tmp";
    const std::string backupName = name + ".bak";

    Status st = fs.writeFile(tempName, bytes);
    if (!st.ok()) {
        return st;
    }

    if (!fileExists(fs, name)) {
        st = fs.renameFile(tempName, name);
        if (st.ok()) {
            return Status::success();
        }
        fs.removeFile(tempName);
        return st;
    }

    fs.removeFile(backupName);
    st = fs.renameFile(name, backupName);
    if (!st.ok()) {
        fs.removeFile(tempName);
        return st;
    }

    st = fs.renameFile(tempName, name);
    if (st.ok()) {
        fs.removeFile(backupName);
        return Status::success();
    }

    fs.renameFile(backupName, name);
    fs.removeFile(tempName);
    return st;
}

Status writeIndexAt(FileSystem& fs, const char* name, const IndexRecord& record) {
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

Status FileStore::emit(Event event) const {
    config_.events.emit(event);
    return event.status;
}

Status FileStore::diagnostic(Severity severity, Status status, const char* operation, std::uint32_t sequence, const char* path) const {
    return emit(Event{
        EventKind::Diagnostic,
        severity,
        status,
        "FileStore",
        operation,
        sequence,
        path,
    });
}

Status FileStore::mount() {
    const auto fs = fileSystem();
    if (!fs) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::BackendUnavailable, "file system backend unavailable"), "mount");
    }
    Status st = fs->mount(config_.basePath);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "mount");
    }
    return Status::success();
}

Status FileStore::readIndex(FileStoreIndex& out) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    const auto fs = fileSystem();

    IndexRecord a;
    IndexRecord b;
    const bool hasA = readIndexAt(*fs, kIndexAName, a);
    const bool hasB = readIndexAt(*fs, kIndexBName, b);

    if (hasA && hasB) {
        out = fromRecord(a.generation >= b.generation ? a : b);
        return Status::success();
    }
    if (hasA) {
        out = fromRecord(a);
        return Status::success();
    }
    if (hasB) {
        out = fromRecord(b);
        return Status::success();
    }

    std::vector<std::string> files;
    st = fs->listFiles(files);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "readIndex");
    }

    std::vector<std::uint32_t> sequences;
    for (const auto& name : files) {
        std::uint32_t sequence = 0;
        if (parseRecordSequence(name, sequence)) {
            sequences.push_back(sequence);
        }
    }

    out = rebuildIndexFromSequences(sequences);
    st = writeIndex(out);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "readIndex");
    }
    return Status::success();
}

Status FileStore::writeIndex(const FileStoreIndex& index) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    if (index.tail < index.head || index.count != index.tail - index.head) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "invalid queue index"), "writeIndex");
    }
    const auto fs = fileSystem();

    IndexRecord currentA;
    IndexRecord currentB;
    const bool hasA = readIndexAt(*fs, kIndexAName, currentA);
    const bool hasB = readIndexAt(*fs, kIndexBName, currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.generation : 0, hasB ? currentB.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.generation <= currentB.generation);
    st = writeIndexAt(*fs, writeA ? kIndexAName : kIndexBName, toRecord(index, generation));
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "writeIndex", kNoSequence, writeA ? kIndexAName : kIndexBName);
    }
    return Status::success();
}

Status FileStore::writeRecord(std::uint32_t sequence, const std::string& record) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    if (record.size() > kMaxRecordBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds file-store maximum"), "writeRecord", sequence);
    }

    const std::string name = recordName(sequence);
    if (name.empty()) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid record sequence"), "writeRecord", sequence);
    }
    RecordHeader header;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);

    std::string bytes = bytesFromObject(&header, sizeof(header));
    bytes.append(record);
    st = writeFileAtomic(*fileSystem(), name, bytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "writeRecord", sequence, name.c_str());
    }
    return Status::success();
}

Status FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }

    const std::string name = recordName(sequence);
    std::string bytes;
    if (name.empty()) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid record sequence"), "readRecord", sequence);
    }
    st = fileSystem()->readFile(name, bytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "readRecord", sequence, name.c_str());
    }
    if (bytes.size() < sizeof(RecordHeader)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "record file is truncated"), "readRecord", sequence, name.c_str());
    }

    RecordHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kFileStoreRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.recordBytes > kMaxRecordBytes) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "record header is invalid"), "readRecord", sequence, name.c_str());
    }

    if (bytes.size() != sizeof(RecordHeader) + header.recordBytes) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "record size does not match header"), "readRecord", sequence, name.c_str());
    }

    std::string record = bytes.substr(sizeof(RecordHeader));
    if (header.crc != recordCrc(header, record)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::CrcMismatch, "record CRC mismatch"), "readRecord", sequence, name.c_str());
    }

    out = std::move(record);
    return Status::success();
}

Status FileStore::removeRecord(std::uint32_t sequence) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }

    const std::string name = recordName(sequence);
    if (name.empty()) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid record sequence"), "removeRecord", sequence);
    }
    st = fileSystem()->removeFile(name);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "removeRecord", sequence, name.c_str());
    }
    return Status::success();
}

std::uint64_t FileStore::freeBytes() const {
    const auto fs = fileSystem();
    if (!fs || !fs->mount(config_.basePath).ok()) {
        return 0;
    }
    return fs->freeBytes();
}

} // namespace pqueue
