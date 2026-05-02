#include "file_store.h"

#include <utility>

namespace pqueue {

FileStore::FileStore(FileStoreConfig config) : config_(std::move(config)) {}

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

} // namespace pqueue
