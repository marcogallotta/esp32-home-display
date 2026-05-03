#include "file_system.h"

#ifndef ARDUINO

#include <cerrno>
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
    Status mount(const std::string& basePath) override {
        basePath_ = basePath;
        std::error_code ec;
        std::filesystem::create_directories(basePath_, ec);
        if (ec) {
            return Status::failure(StatusCode::MountFailed, "failed to create storage directory", ec.value());
        }
        return Status::success();
    }

    Status readFile(const std::string& name, std::string& out) override {
        errno = 0;
        std::ifstream file(path(name), std::ios::binary);
        if (!file) {
            return Status::failure(StatusCode::ReadFailed, "failed to open file for read", errno);
        }
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!file.good() && !file.eof()) {
            return Status::failure(StatusCode::ReadFailed, "failed to read file", errno);
        }
        return Status::success();
    }

    Status writeFile(const std::string& name, const std::string& data) override {
        errno = 0;
        std::ofstream file(path(name), std::ios::binary | std::ios::trunc);
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to open file for write", errno);
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file.good()) {
            return Status::failure(StatusCode::WriteFailed, "failed to write file", errno);
        }
        return Status::success();
    }

    Status removeFile(const std::string& name) override {
        std::error_code ec;
        std::filesystem::remove(path(name), ec);
        if (ec) {
            return Status::failure(StatusCode::RemoveFailed, "failed to remove file", ec.value());
        }
        return Status::success();
    }

    Status renameFile(const std::string& fromName, const std::string& toName) override {
        std::error_code ec;
        std::filesystem::rename(path(fromName), path(toName), ec);
        if (ec) {
            return Status::failure(StatusCode::RenameFailed, "failed to rename file", ec.value());
        }
        return Status::success();
    }

    Status listFiles(std::vector<std::string>& out) override {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(basePath_, ec)) {
            if (ec) {
                return Status::failure(StatusCode::ListFailed, "failed to list storage directory", ec.value());
            }
            out.push_back(entry.path().filename().string());
        }
        if (ec) {
            return Status::failure(StatusCode::ListFailed, "failed to list storage directory", ec.value());
        }
        return Status::success();
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
