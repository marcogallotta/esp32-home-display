#pragma once

#include <cstdint>
#include <string>

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
};

struct FileStoreIndex {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

// TODO: add optional storage error reporting/logging once the public logging model is decided.
class FileStore {
public:
    explicit FileStore(FileStoreConfig config = FileStoreConfig{});
    explicit FileStore(std::string basePath);

    bool mount();
    bool readIndex(FileStoreIndex& out);
    bool writeIndex(const FileStoreIndex& index);

    bool writeRecord(std::uint32_t sequence, const std::string& record);
    bool readRecord(std::uint32_t sequence, std::string& out);
    bool removeRecord(std::uint32_t sequence);

    std::uint64_t freeBytes() const;

private:
    StorageBackend resolvedBackend() const;

    FileStoreConfig config_;
};

} // namespace pqueue
