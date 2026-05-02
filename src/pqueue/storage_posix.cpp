#include "file_system.h"

#ifndef ARDUINO

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace pqueue {
namespace {

class PosixFileSystem final : public FileSystem {
public:
    bool mount(const std::string& basePath) override {
        basePath_ = basePath;
        std::error_code ec;
        std::filesystem::create_directories(basePath_, ec);
        return !ec;
    }

    bool readFile(const std::string& name, std::string& out) override {
        std::ifstream file(path(name), std::ios::binary);
        if (!file) {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return file.good() || file.eof();
    }

    bool writeFile(const std::string& name, const std::string& data) override {
        std::ofstream file(path(name), std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        return file.good();
    }

    bool removeFile(const std::string& name) override {
        std::error_code ec;
        std::filesystem::remove(path(name), ec);
        return !ec;
    }

    bool renameFile(const std::string& fromName, const std::string& toName) override {
        std::error_code ec;
        std::filesystem::rename(path(fromName), path(toName), ec);
        return !ec;
    }

    bool listFiles(std::vector<std::string>& out) override {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(basePath_, ec)) {
            if (ec) {
                return false;
            }
            out.push_back(entry.path().filename().string());
        }
        return !ec;
    }

    std::uint64_t freeBytes() const override {
        std::error_code ec;
        const auto info = std::filesystem::space(basePath_, ec);
        return ec ? 0 : static_cast<std::uint64_t>(info.available);
    }

private:
    std::filesystem::path path(const std::string& name) const {
        return std::filesystem::path(basePath_) / name;
    }

    std::string basePath_;
};

} // namespace

std::shared_ptr<FileSystem> makePosixFileSystem() {
    return std::make_shared<PosixFileSystem>();
}

std::shared_ptr<FileSystem> makeLittleFsFileSystem() {
    return nullptr;
}

} // namespace pqueue

#endif // !ARDUINO
