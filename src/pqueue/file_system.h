#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "status.h"

namespace pqueue {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual Status mount(const std::string& basePath) = 0;
    virtual Status readFile(const std::string& name, std::string& out) = 0;
    virtual Status writeFile(const std::string& name, const std::string& data) = 0;
    virtual Status removeFile(const std::string& name) = 0;
    virtual Status renameFile(const std::string& fromName, const std::string& toName) = 0;
    virtual Status listFiles(std::vector<std::string>& out) = 0;
    virtual Status tryAcquireLockFile(const std::string& name) = 0;
    virtual Status releaseLockFile(const std::string& name) = 0;
    virtual std::uint64_t freeBytes() const = 0;
};

std::shared_ptr<FileSystem> makePosixFileSystem();
std::shared_ptr<FileSystem> makeLittleFsFileSystem();

} // namespace pqueue
