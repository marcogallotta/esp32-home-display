#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace pqueue {

using Record = std::string;

struct Config {
    std::uint32_t diskReserveBytes = 256 * 1024;
    std::size_t maxRecordBytes = 4096;
    // TODO: make full-queue behavior configurable instead of always rejecting newest.
};

struct Stats {
    std::uint32_t count = 0;
    std::uint64_t freeBytes = 0;
};

} // namespace pqueue
