#pragma once

#include <cstdint>
#include <string>

namespace pqueue {

struct FileStoreIndex {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

class FileStore {
public:
    explicit FileStore(std::string basePath = "pqueue_spool");

    bool mount();
    bool readIndex(FileStoreIndex& out);
    bool writeIndex(const FileStoreIndex& index);

    bool writeRecord(std::uint32_t sequence, const std::string& record);
    bool readRecord(std::uint32_t sequence, std::string& out);
    bool removeRecord(std::uint32_t sequence);

    std::uint64_t freeBytes() const;

private:
    std::string basePath_;
};

} // namespace pqueue
