#pragma once

#include <cstdint>

namespace pqueue {
struct Record;
}

namespace api {

struct RecordStoreIndex {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

class RecordStore {
public:
    virtual ~RecordStore() = default;

    virtual bool readIndex(RecordStoreIndex& out) = 0;
    virtual bool writeIndex(const RecordStoreIndex& index) = 0;

    virtual bool writeRecord(std::uint32_t sequence, const pqueue::Record& request) = 0;
    virtual bool readRecord(std::uint32_t sequence, pqueue::Record& out) = 0;
    virtual bool removeRecord(std::uint32_t sequence) = 0;

    virtual std::uint64_t freeBytes() = 0;
};

} // namespace api
