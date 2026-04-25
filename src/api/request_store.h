#pragma once

#include <cstdint>

namespace api {

struct BufferedRequest;

struct RequestStoreIndex {
    std::uint32_t head = 0;
    std::uint32_t tail = 0;
    std::uint32_t count = 0;
};

class RequestStore {
public:
    virtual ~RequestStore() = default;

    virtual bool readIndex(RequestStoreIndex& out) = 0;
    virtual bool writeIndex(const RequestStoreIndex& index) = 0;

    virtual bool writeRequest(std::uint32_t sequence, const BufferedRequest& request) = 0;
    virtual bool readRequest(std::uint32_t sequence, BufferedRequest& out) = 0;
    virtual bool removeRequest(std::uint32_t sequence) = 0;

    virtual std::uint64_t freeBytes() = 0;
};

} // namespace api
