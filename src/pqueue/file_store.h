#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "events.h"
#include "file_system.h"
#include "status.h"

namespace pqueue {

enum class StorageBackend {
    Default,
    Posix,
    LittleFS,
};

struct FileStoreConfig {
#ifdef ARDUINO
    std::string basePath = "/pqueue_spool";
#else
    std::string basePath = "pqueue_spool";
#endif
    StorageBackend backend = StorageBackend::Default;
    std::shared_ptr<FileSystem> fileSystem;
    std::uint32_t reservedBytes = 128 * 1024;
    std::size_t recordSizeBytes = 4096;
    EventOptions events;
};

struct FileStoreIndex {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

class FileStore {
public:
    explicit FileStore(FileStoreConfig config = FileStoreConfig{});
    explicit FileStore(std::string basePath);

    Status mount();
    Status readIndex(FileStoreIndex& out);
    Status writeIndex(const FileStoreIndex& index);

    Status writeRecord(std::uint32_t sequence, const std::string& record);
    Status readRecord(std::uint32_t sequence, std::string& out);
    Status removeRecord(std::uint32_t sequence);

    Status tryAcquireLockFile(const std::string& name, const std::string& contents);
    Status releaseLockFile(const std::string& name, const std::string& expectedContents);

    std::uint64_t freeBytes() const;

private:
    StorageBackend resolvedBackend() const;
    std::shared_ptr<FileSystem> fileSystem() const;
    Status emit(Event event) const;
    Status diagnostic(Severity severity, Status status, const char* operation, std::uint32_t sequence = kNoSequence, const char* path = "") const;

    FileStoreConfig config_;
    mutable std::shared_ptr<FileSystem> fileSystem_;
};

} // namespace pqueue
