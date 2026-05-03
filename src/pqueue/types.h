#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "events.h"
#include "file_store.h"

namespace pqueue {

using Record = std::string;

struct Config {
#ifdef ARDUINO
    std::string basePath = "/pqueue_spool";
#else
    std::string basePath = "pqueue_spool";
#endif
    StorageBackend storageBackend = StorageBackend::Default;
    std::uint32_t diskReserveBytes = 128 * 1024;
    std::size_t maxRecordBytes = 4096;
    EventOptions events;
    // TODO: make full-queue behavior configurable instead of always rejecting newest.
};

struct Stats {
    std::uint32_t count = 0;
    std::uint64_t freeBytes = 0;
};

} // namespace pqueue
