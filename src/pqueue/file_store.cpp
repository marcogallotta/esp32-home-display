#include "file_store.h"
#include "storage_common.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace pqueue {
namespace {

using namespace storage_detail;

constexpr const char* kIndexAName = "pqueue.meta_a";
constexpr const char* kIndexBName = "pqueue.meta_b";
constexpr const char* kSpoolName = "pqueue.spool";

std::string bytesFromObject(const void* data, std::size_t size) {
    return std::string(static_cast<const char*>(data), size);
}

bool fileExists(FileSystem& fs, const std::string& name) {
    std::uint64_t ignored = 0;
    return fs.fileSize(name, ignored).ok();
}

Status requireFileSize(FileSystem& fs, const std::string& name, std::uint64_t expected) {
    std::uint64_t actual = 0;
    Status st = fs.fileSize(name, actual);
    if (!st.ok()) {
        return Status::failure(StatusCode::ReadFailed, "pqueue storage file is missing");
    }
    if (actual != expected) {
        return Status::failure(StatusCode::InvalidIndex, "pqueue spool size does not match configured storage layout");
    }
    return Status::success();
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

struct Layout {
    std::uint32_t capacityRecords = 0;
    std::uint32_t recordSizeBytes = 0;
    std::uint32_t reservedBytes = 0;
    std::uint32_t slotSizeBytes = 0;
    std::uint32_t spoolBytes = 0;
};

bool makeLayout(const FileStoreConfig& config, Layout& out) {
    if (config.recordSizeBytes == 0 || config.reservedBytes == 0) {
        return false;
    }
    if (config.recordSizeBytes > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto slotSize = sizeof(RecordHeader) + config.recordSizeBytes;
    if (slotSize > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto capacity = config.reservedBytes / slotSize;
    if (capacity == 0 || capacity > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out.capacityRecords = static_cast<std::uint32_t>(capacity);
    out.recordSizeBytes = static_cast<std::uint32_t>(config.recordSizeBytes);
    out.reservedBytes = config.reservedBytes;
    out.slotSizeBytes = static_cast<std::uint32_t>(slotSize);
    out.spoolBytes = out.capacityRecords * out.slotSizeBytes;
    return true;
}

std::uint64_t slotOffset(const Layout& layout, std::uint32_t sequence) {
    return static_cast<std::uint64_t>(sequence % layout.capacityRecords) * layout.slotSizeBytes;
}

struct ReadIndexResult {
    bool exists = false;
    bool configMismatch = false;
    IndexRecord record;
};

ReadIndexResult readIndexAt(FileSystem& fs, const char* name, const Layout& layout) {
    ReadIndexResult out;
    std::string bytes;
    const Status read = fs.readFile(name, bytes);
    if (!read.ok()) {
        return out;
    }
    out.exists = true;
    if (bytes.size() != sizeof(out.record)) {
        return out;
    }
    std::memcpy(&out.record, bytes.data(), sizeof(out.record));
    if (!validIndexShape(out.record)) {
        return out;
    }
    out.configMismatch = !validIndexForConfig(out.record, layout.capacityRecords, layout.recordSizeBytes, layout.reservedBytes);
    return out;
}

bool usableIndex(const ReadIndexResult& result) {
    return result.exists && validIndexShape(result.record) && !result.configMismatch;
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
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config: reservedBytes must fit at least one recordSizeBytes slot"), "mount");
    }

    const auto a = readIndexAt(*fs, kIndexAName, layout);
    const auto b = readIndexAt(*fs, kIndexBName, layout);
    if (a.configMismatch || b.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "mount");
    }

    const bool hasAnyMetadata = a.exists || b.exists;
    const bool hasUsableMetadata = usableIndex(a) || usableIndex(b);
    const bool hasSpool = fileExists(*fs, kSpoolName);

    if (!hasAnyMetadata) {
        if (hasSpool) {
            return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue spool exists but metadata is missing; delete or reformat queue files to continue"), "mount", kNoSequence, kSpoolName);
        }
        st = fs->resizeFile(kSpoolName, layout.spoolBytes);
        if (!st.ok()) {
            return diagnostic(Severity::Error, st, "mount", kNoSequence, kSpoolName);
        }
        const IndexRecord empty = toRecord(FileStoreIndex{}, 1, layout.capacityRecords, layout.recordSizeBytes, layout.reservedBytes);
        st = writeFileAtomic(*fs, kIndexAName, bytesFromObject(&empty, sizeof(empty)));
        if (!st.ok()) {
            return diagnostic(Severity::Error, st, "mount", kNoSequence, kIndexAName);
        }
        return Status::success();
    }

    if (!hasUsableMetadata) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue metadata is corrupt or invalid; delete or repair queue files to continue"), "mount");
    }

    st = requireFileSize(*fs, kSpoolName, layout.spoolBytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "mount", kNoSequence, kSpoolName);
    }

    return Status::success();
}

Status FileStore::readIndex(FileStoreIndex& out) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "readIndex");
    }
    const auto fs = fileSystem();

    const auto a = readIndexAt(*fs, kIndexAName, layout);
    const auto b = readIndexAt(*fs, kIndexBName, layout);

    if (a.configMismatch || b.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "readIndex");
    }

    const bool hasA = usableIndex(a);
    const bool hasB = usableIndex(b);
    if (hasA && hasB) {
        out = fromRecord(a.record.generation >= b.record.generation ? a.record : b.record);
        return Status::success();
    }
    if (hasA) {
        out = fromRecord(a.record);
        return Status::success();
    }
    if (hasB) {
        out = fromRecord(b.record);
        return Status::success();
    }

    out = FileStoreIndex{};
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
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "writeIndex");
    }
    if (index.tail < index.head || index.count != index.tail - index.head || index.count > layout.capacityRecords) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "invalid queue index"), "writeIndex");
    }
    const auto fs = fileSystem();

    const auto currentA = readIndexAt(*fs, kIndexAName, layout);
    const auto currentB = readIndexAt(*fs, kIndexBName, layout);
    if (currentA.configMismatch || currentB.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "writeIndex");
    }
    const bool hasA = usableIndex(currentA);
    const bool hasB = usableIndex(currentB);
    const std::uint32_t generation = std::max(hasA ? currentA.record.generation : 0, hasB ? currentB.record.generation : 0) + 1;
    const bool writeA = !hasA || (hasB && currentA.record.generation <= currentB.record.generation);
    const IndexRecord record = toRecord(index, generation, layout.capacityRecords, layout.recordSizeBytes, layout.reservedBytes);
    st = writeFileAtomic(*fs, writeA ? kIndexAName : kIndexBName, bytesFromObject(&record, sizeof(record)));
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
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "writeRecord", sequence);
    }
    if (record.size() > layout.recordSizeBytes) {
        return diagnostic(Severity::Warning, Status::failure(StatusCode::RecordTooLarge, "record exceeds configured pqueue slot size"), "writeRecord", sequence);
    }

    RecordHeader header;
    header.sequence = sequence;
    header.recordBytes = static_cast<std::uint32_t>(record.size());
    header.crc = recordCrc(header, record);

    std::string bytes = bytesFromObject(&header, sizeof(header));
    bytes.append(record);
    bytes.resize(layout.slotSizeBytes, '\0');

    st = fileSystem()->writeAt(kSpoolName, slotOffset(layout, sequence), bytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "writeRecord", sequence, kSpoolName);
    }
    return Status::success();
}

Status FileStore::readRecord(std::uint32_t sequence, std::string& out) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "readRecord", sequence);
    }

    std::string bytes;
    st = fileSystem()->readAt(kSpoolName, slotOffset(layout, sequence), layout.slotSizeBytes, bytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "readRecord", sequence, kSpoolName);
    }
    if (bytes.size() != layout.slotSizeBytes || bytes.size() < sizeof(RecordHeader)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "spool slot is truncated"), "readRecord", sequence, kSpoolName);
    }

    RecordHeader header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != kFileStoreRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != sizeof(RecordHeader) ||
        header.sequence != sequence ||
        header.recordBytes > layout.recordSizeBytes) {
        // TODO: expose an fsck/recovery path that can log and repair/drop corrupt front records.
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "spool slot header is invalid"), "readRecord", sequence, kSpoolName);
    }

    std::string record = bytes.substr(sizeof(RecordHeader), header.recordBytes);
    if (header.crc != recordCrc(header, record)) {
        // TODO: expose an fsck/recovery path that can log and repair/drop corrupt front records.
        return diagnostic(Severity::Error, Status::failure(StatusCode::CrcMismatch, "spool slot CRC mismatch"), "readRecord", sequence, kSpoolName);
    }

    out = std::move(record);
    return Status::success();
}

Status FileStore::removeRecord(std::uint32_t sequence) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "removeRecord", sequence);
    }
    const std::string empty(layout.slotSizeBytes, '\0');
    st = fileSystem()->writeAt(kSpoolName, slotOffset(layout, sequence), empty);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "removeRecord", sequence, kSpoolName);
    }
    return Status::success();
}

Status FileStore::tryAcquireLockFile(const std::string& name, const std::string& contents) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    return fileSystem()->tryAcquireLockFile(name, contents);
}

Status FileStore::releaseLockFile(const std::string& name, const std::string& expectedContents) {
    Status st = mount();
    if (!st.ok()) {
        return st;
    }
    return fileSystem()->releaseLockFile(name, expectedContents);
}

std::uint64_t FileStore::freeBytes() const {
    const auto fs = fileSystem();
    if (!fs || !fs->mount(config_.basePath).ok()) {
        return 0;
    }
    return fs->freeBytes();
}

} // namespace pqueue
