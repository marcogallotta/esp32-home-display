#include "file_store.h"
#include "storage_common.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace pqueue {
namespace {

using namespace storage_detail;

constexpr const char* kSpoolName = "pqueue.spool";
constexpr std::uint32_t kJournalFullPercent = 75;

bool fileExists(FileSystem& fs, const std::string& name) {
    std::uint64_t ignored = 0;
    return fs.fileSize(name, ignored).ok();
}

struct Layout {
    std::uint32_t capacityRecords = 0;
    std::uint32_t recordSizeBytes = 0;
    std::uint32_t reservedBytes = 0;
    std::uint32_t journalBytes = 0;
    std::uint32_t checkpointEveryOps = 0;
    std::uint32_t slotSizeBytes = 0;
    std::uint32_t checkpointBytes = 0;
    std::uint32_t recordRegionOffset = 0;
    std::uint32_t spoolBytes = 0;
};

bool makeLayout(const FileStoreConfig& config, Layout& out) {
    if (config.recordSizeBytes == 0 || config.reservedBytes == 0 || config.journalBytes < kJournalEntryBytes || config.checkpointEveryOps == 0) {
        return false;
    }
    if (config.recordSizeBytes > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto slotSize = static_cast<std::uint64_t>(kRecordHeaderBytes) + config.recordSizeBytes;
    if (slotSize > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto capacity = config.reservedBytes / slotSize;
    if (capacity == 0 || capacity > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto checkpointBytes = static_cast<std::uint64_t>(kCheckpointSlots) * kCheckpointRecordBytes;
    const auto recordRegionOffset = checkpointBytes + config.journalBytes;
    const auto spoolBytes = recordRegionOffset + capacity * slotSize;
    if (spoolBytes > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    out.capacityRecords = static_cast<std::uint32_t>(capacity);
    out.recordSizeBytes = static_cast<std::uint32_t>(config.recordSizeBytes);
    out.reservedBytes = config.reservedBytes;
    out.journalBytes = config.journalBytes;
    out.checkpointEveryOps = config.checkpointEveryOps;
    out.slotSizeBytes = static_cast<std::uint32_t>(slotSize);
    out.checkpointBytes = static_cast<std::uint32_t>(checkpointBytes);
    out.recordRegionOffset = static_cast<std::uint32_t>(recordRegionOffset);
    out.spoolBytes = static_cast<std::uint32_t>(spoolBytes);
    return true;
}

std::uint64_t checkpointOffset(std::uint32_t slot) {
    return static_cast<std::uint64_t>(slot) * kCheckpointRecordBytes;
}

std::uint64_t journalOffset(const Layout& layout, std::uint32_t usedBytes) {
    return static_cast<std::uint64_t>(layout.checkpointBytes) + usedBytes;
}

std::uint64_t slotOffset(const Layout& layout, std::uint32_t sequence) {
    return static_cast<std::uint64_t>(layout.recordRegionOffset) +
           static_cast<std::uint64_t>(sequence % layout.capacityRecords) * layout.slotSizeBytes;
}

bool ringStateMatches(const CheckpointRecord& record) {
    return record.count <= record.capacityRecords &&
           record.tail >= record.head &&
           record.count == record.tail - record.head;
}

void addValidationError(ValidationResult& result, const ValidationOptions& options, ValidationIssue issue) {
    result.ok = false;
    if (result.errors.size() < options.maxErrors) {
        result.errors.push_back(std::move(issue));
    } else {
        result.stoppedEarly = true;
    }
}

ValidationIssue makeIssue(ValidationIssueCode code, std::string message) {
    ValidationIssue issue;
    issue.code = code;
    issue.message = std::move(message);
    return issue;
}

ValidationIssue makeSlotIssue(ValidationIssueCode code, std::string message, std::uint32_t slotIndex, std::uint32_t expectedSequence) {
    ValidationIssue issue = makeIssue(code, std::move(message));
    issue.slotIndex = slotIndex;
    issue.expectedSequence = expectedSequence;
    issue.hasSlotIndex = true;
    issue.hasExpectedSequence = true;
    return issue;
}

struct StoredState {
    bool foundCheckpoint = false;
    bool configMismatch = false;
    CheckpointRecord checkpoint;
    FileStoreIndex index;
    std::uint32_t journalUsedBytes = 0;
    std::uint32_t journalOps = 0;
};

bool applyJournalEntry(const JournalEntry& entry, FileStoreIndex& index) {
    switch (entry.op) {
    case JournalOp::Enqueue:
        if (entry.sequence != index.tail) {
            return false;
        }
        ++index.tail;
        ++index.count;
        return true;
    case JournalOp::Pop:
        if (index.count == 0 || entry.sequence != index.head) {
            return false;
        }
        ++index.head;
        --index.count;
        return true;
    case JournalOp::RewriteFront:
        return index.count > 0 && entry.sequence >= index.head && entry.sequence < index.tail;
    }
    return false;
}

StoredState loadStoredState(FileSystem& fs, const Layout& layout) {
    StoredState out;
    for (std::uint32_t slot = 0; slot < kCheckpointSlots; ++slot) {
        std::string bytes;
        if (!fs.readAt(kSpoolName, checkpointOffset(slot), kCheckpointRecordBytes, bytes).ok()) {
            continue;
        }
        CheckpointRecord candidate;
        if (!parseCheckpointRecord(bytes, candidate) || !validCheckpointShape(candidate)) {
            continue;
        }
        if (!validCheckpointForConfig(candidate, layout.capacityRecords, layout.recordSizeBytes, layout.reservedBytes, layout.journalBytes)) {
            out.configMismatch = true;
            continue;
        }
        if (!out.foundCheckpoint || candidate.generation > out.checkpoint.generation) {
            out.foundCheckpoint = true;
            out.checkpoint = candidate;
        }
    }

    if (!out.foundCheckpoint) {
        return out;
    }

    out.index = fromCheckpointRecord(out.checkpoint);
    out.journalUsedBytes = 0;
    std::uint32_t offset = 0;
    std::uint32_t nextGeneration = out.checkpoint.generation + 1;
    while (offset + kJournalEntryBytes <= layout.journalBytes) {
        std::string bytes;
        if (!fs.readAt(kSpoolName, journalOffset(layout, offset), kJournalEntryBytes, bytes).ok()) {
            break;
        }
        JournalEntry entry;
        if (!parseJournalEntry(bytes, entry) || !validJournalEntryShape(entry) || entry.generation != nextGeneration) {
            break;
        }
        if (!applyJournalEntry(entry, out.index)) {
            break;
        }
        offset += kJournalEntryBytes;
        ++nextGeneration;
        ++out.journalOps;
    }
    out.journalUsedBytes = offset;
    return out;
}

Status writeCheckpoint(FileSystem& fs, const Layout& layout, const FileStoreIndex& index, std::uint32_t generation) {
    const CheckpointRecord record = toCheckpointRecord(index, generation, layout.capacityRecords, layout.recordSizeBytes, layout.reservedBytes, layout.journalBytes, 0);
    const std::uint32_t slot = generation % kCheckpointSlots;
    return fs.writeAt(kSpoolName, checkpointOffset(slot), serializeCheckpointRecord(record));
}

bool shouldCheckpoint(const Layout& layout, const StoredState& state) {
    const std::uint32_t nextUsed = state.journalUsedBytes + kJournalEntryBytes;
    if (nextUsed > layout.journalBytes) {
        return true;
    }
    if (state.journalOps + 1 >= layout.checkpointEveryOps) {
        return true;
    }
    return nextUsed * 100U >= layout.journalBytes * kJournalFullPercent;
}

Status commitIndexTransition(FileSystem& fs, const Layout& layout, const StoredState& state, const FileStoreIndex& next) {
    JournalEntry entry;
    bool knownTransition = true;

    if (next.head == state.index.head && next.tail == state.index.tail + 1 && next.count == state.index.count + 1) {
        entry.op = JournalOp::Enqueue;
        entry.sequence = state.index.tail;
    } else if (next.head == state.index.head + 1 && next.tail == state.index.tail && next.count + 1 == state.index.count) {
        entry.op = JournalOp::Pop;
        entry.sequence = state.index.head;
    } else if (next.head == state.index.head && next.tail == state.index.tail && next.count == state.index.count) {
        return Status::success();
    } else {
        knownTransition = false;
    }

    if (!knownTransition || shouldCheckpoint(layout, state)) {
        return writeCheckpoint(fs, layout, next, state.checkpoint.generation + state.journalOps + 1);
    }

    entry.generation = state.checkpoint.generation + state.journalOps + 1;
    entry.crc = journalCrc(entry);
    return fs.writeAt(kSpoolName, journalOffset(layout, state.journalUsedBytes), serializeJournalEntry(entry));
}

Status commitRewrite(FileSystem& fs, const Layout& layout, const StoredState& state, std::uint32_t sequence) {
    if (shouldCheckpoint(layout, state)) {
        return writeCheckpoint(fs, layout, state.index, state.checkpoint.generation + state.journalOps + 1);
    }
    JournalEntry entry;
    entry.op = JournalOp::RewriteFront;
    entry.sequence = sequence;
    entry.generation = state.checkpoint.generation + state.journalOps + 1;
    entry.crc = journalCrc(entry);
    return fs.writeAt(kSpoolName, journalOffset(layout, state.journalUsedBytes), serializeJournalEntry(entry));
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
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config: reservedBytes must fit at least one recordSizeBytes slot and journalBytes must fit at least one entry"), "mount");
    }

    const bool hasSpool = fileExists(*fs, kSpoolName);
    if (!hasSpool) {
        st = fs->resizeFile(kSpoolName, layout.spoolBytes);
        if (!st.ok()) {
            return diagnostic(Severity::Error, st, "mount", kNoSequence, kSpoolName);
        }
        st = writeCheckpoint(*fs, layout, FileStoreIndex{}, 1);
        if (!st.ok()) {
            return diagnostic(Severity::Error, st, "mount", kNoSequence, kSpoolName);
        }
        return Status::success();
    }

    st = [&] {
        std::uint64_t actual = 0;
        Status sizeStatus = fs->fileSize(kSpoolName, actual);
        if (!sizeStatus.ok()) {
            return Status::failure(StatusCode::ReadFailed, "pqueue spool file is missing");
        }
        if (actual != layout.spoolBytes) {
            return Status::failure(StatusCode::InvalidIndex, "pqueue spool size does not match configured storage layout");
        }
        return Status::success();
    }();
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "mount", kNoSequence, kSpoolName);
    }

    const StoredState state = loadStoredState(*fs, layout);
    if (state.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "mount");
    }
    if (!state.foundCheckpoint) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue checkpoint metadata is corrupt or missing; delete or repair queue files to continue"), "mount");
    }
    return Status::success();
}

ValidationResult FileStore::validateUnlocked(const ValidationOptions& options) {
    ValidationResult result;

    const auto fs = fileSystem();
    if (!fs) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::InvalidConfig, "file system backend unavailable"));
        return result;
    }

    Status st = fs->mount(config_.basePath);
    if (!st.ok()) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::InvalidConfig, st.message));
        return result;
    }

    Layout layout;
    if (!makeLayout(config_, layout)) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::InvalidConfig, "invalid pqueue storage config"));
        return result;
    }

    if (!fileExists(*fs, kSpoolName)) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::SpoolMissing, "pqueue spool file is missing"));
        return result;
    }

    std::uint64_t actualSpoolBytes = 0;
    st = fs->fileSize(kSpoolName, actualSpoolBytes);
    if (!st.ok()) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::SpoolMissing, "pqueue spool file is missing"));
        return result;
    }
    if (actualSpoolBytes != layout.spoolBytes) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::SpoolSizeMismatch, "pqueue spool size does not match configured storage layout"));
        return result;
    }

    const StoredState state = loadStoredState(*fs, layout);
    if (state.configMismatch) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::ConfigMismatch, "pqueue checkpoint config does not match current storage config"));
        return result;
    }
    if (!state.foundCheckpoint) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::MetadataCorrupt, "no usable pqueue checkpoint found"));
        return result;
    }

    CheckpointRecord active = state.checkpoint;
    active.head = state.index.head;
    active.tail = state.index.tail;
    active.count = state.index.count;
    active.journalUsedBytes = state.journalUsedBytes;
    if (!ringStateMatches(active)) {
        addValidationError(result, options, makeIssue(ValidationIssueCode::InvalidRingState, "pqueue metadata ring state is invalid"));
        return result;
    }

    for (std::uint32_t sequence = state.index.head; sequence < state.index.tail; ++sequence) {
        const std::uint32_t slotIndex = sequence % layout.capacityRecords;
        std::string bytes;
        st = fs->readAt(kSpoolName, slotOffset(layout, sequence), layout.slotSizeBytes, bytes);
        if (!st.ok() || bytes.size() != layout.slotSizeBytes || bytes.size() < kRecordHeaderBytes) {
            addValidationError(result, options, makeSlotIssue(ValidationIssueCode::SlotReadFailed, "failed to read active pqueue spool slot", slotIndex, sequence));
            if (result.stoppedEarly) {
                return result;
            }
            continue;
        }

        RecordHeader header;
        if (!parseRecordHeader(bytes, header) ||
            header.magic != kFileStoreRecordMagic ||
            header.version != kFormatVersion ||
            header.headerBytes != kRecordHeaderBytes ||
            header.sequence != sequence ||
            header.recordBytes > layout.recordSizeBytes) {
            auto issue = makeSlotIssue(ValidationIssueCode::SlotHeaderInvalid, "active pqueue spool slot header is invalid", slotIndex, sequence);
            issue.actualSequence = header.sequence;
            issue.hasActualSequence = true;
            addValidationError(result, options, std::move(issue));
            if (result.stoppedEarly) {
                return result;
            }
            continue;
        }

        const std::string record = bytes.substr(kRecordHeaderBytes, header.recordBytes);
        if (header.crc != recordCrc(header, record)) {
            addValidationError(result, options, makeSlotIssue(ValidationIssueCode::SlotCrcMismatch, "active pqueue spool slot CRC mismatch", slotIndex, sequence));
            if (result.stoppedEarly) {
                return result;
            }
            continue;
        }

        ++result.checkedRecords;
    }

    return result;
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
    const StoredState state = loadStoredState(*fileSystem(), layout);
    if (state.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "readIndex");
    }
    if (!state.foundCheckpoint) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue checkpoint metadata is corrupt or missing"), "readIndex");
    }
    out = state.index;
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
    const StoredState state = loadStoredState(*fs, layout);
    if (state.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue storage config changed; delete or reformat queue files to continue"), "writeIndex");
    }
    if (!state.foundCheckpoint) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue checkpoint metadata is corrupt or missing"), "writeIndex");
    }
    st = commitIndexTransition(*fs, layout, state, index);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "writeIndex", kNoSequence, kSpoolName);
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

    std::string bytes = serializeRecordHeader(header);
    bytes.append(record);
    bytes.resize(layout.slotSizeBytes, '\0');

    st = fileSystem()->writeAt(kSpoolName, slotOffset(layout, sequence), bytes);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "writeRecord", sequence, kSpoolName);
    }
    return Status::success();
}

Status FileStore::rewriteRecord(std::uint32_t sequence, const std::string& record) {
    Status st = writeRecord(sequence, record);
    if (!st.ok()) {
        return st;
    }
    Layout layout;
    if (!makeLayout(config_, layout)) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidArgument, "invalid pqueue storage config"), "rewriteRecord", sequence);
    }
    const auto fs = fileSystem();
    const StoredState state = loadStoredState(*fs, layout);
    if (!state.foundCheckpoint || state.configMismatch) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidIndex, "pqueue checkpoint metadata is corrupt, missing, or mismatched"), "rewriteRecord", sequence);
    }
    st = commitRewrite(*fs, layout, state, sequence);
    if (!st.ok()) {
        return diagnostic(Severity::Error, st, "rewriteRecord", sequence, kSpoolName);
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
    if (bytes.size() != layout.slotSizeBytes || bytes.size() < kRecordHeaderBytes) {
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "spool slot is truncated"), "readRecord", sequence, kSpoolName);
    }

    RecordHeader header;
    if (!parseRecordHeader(bytes, header) ||
        header.magic != kFileStoreRecordMagic ||
        header.version != kFormatVersion ||
        header.headerBytes != kRecordHeaderBytes ||
        header.sequence != sequence ||
        header.recordBytes > layout.recordSizeBytes) {
        // TODO: expose an fsck/recovery path that can log and repair/drop corrupt front records.
        return diagnostic(Severity::Error, Status::failure(StatusCode::InvalidRecord, "spool slot header is invalid"), "readRecord", sequence, kSpoolName);
    }

    std::string record = bytes.substr(kRecordHeaderBytes, header.recordBytes);
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
