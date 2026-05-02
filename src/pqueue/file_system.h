#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pqueue {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual bool mount(const std::string& basePath) = 0;
    virtual bool readFile(const std::string& name, std::string& out) = 0;
    virtual bool writeFile(const std::string& name, const std::string& data) = 0;
    virtual bool removeFile(const std::string& name) = 0;
    virtual bool renameFile(const std::string& fromName, const std::string& toName) = 0;
    virtual bool listFiles(std::vector<std::string>& out) = 0;
    virtual std::uint64_t freeBytes() const = 0;
};

std::shared_ptr<FileSystem> makePosixFileSystem();
std::shared_ptr<FileSystem> makeLittleFsFileSystem();

} // namespace pqueue
