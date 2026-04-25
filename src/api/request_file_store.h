#pragma once

#include <cstdint>

#include "request_store.h"

namespace api::request_file_store {

struct Index {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

bool mount();

bool readIndex(Index& out);
bool writeIndex(const Index& index);

bool writeRequest(std::uint32_t sequence, const BufferedRequest& request);
bool readRequest(std::uint32_t sequence, BufferedRequest& out);
bool removeRequest(std::uint32_t sequence);

std::uint64_t freeBytes();

RequestStore& defaultStore();

} // namespace api::request_file_store
