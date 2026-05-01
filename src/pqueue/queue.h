#pragma once

#include <cstdint>
#include <string>

#include "file_store.h"
#include "types.h"

namespace pqueue {

class Queue {
public:
    Queue(FileStore& store, Config config = Config{});

    bool enqueue(const std::string& record);
    bool peek(std::string& out);
    bool pop();
    bool rewriteFront(const std::string& record);
    Stats stats();

private:
    bool ensureLoaded();

    FileStore& store_;
    Config config_;
    FileStoreIndex index_;
    bool loaded_ = false;
};

} // namespace pqueue
