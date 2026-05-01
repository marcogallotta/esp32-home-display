#pragma once

#include <cstdint>

#include "record_store.h"

namespace api::record_file_store {

struct Index {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

bool mount();
void setBasePath(const char* path);

bool readIndex(Index& out);
bool writeIndex(const Index& index);

bool writeRecord(std::uint32_t sequence, const pqueue::Record& request);
bool readRecord(std::uint32_t sequence, pqueue::Record& out);
bool removeRecord(std::uint32_t sequence);

std::uint64_t freeBytes();

RecordStore& defaultStore();

} // namespace api::record_file_store
