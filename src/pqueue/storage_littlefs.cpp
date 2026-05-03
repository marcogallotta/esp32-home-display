#include "file_system.h"

#ifdef ARDUINO

#include <LittleFS.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace pqueue {
namespace {

std::string normalizeBasePath(const std::string& basePath) {
    if (basePath.empty() || basePath == "/") {
        return "/";
    }
    std::string out = basePath.front() == '/' ? basePath : "/" + basePath;
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base == "/") {
        return "/" + name;
    }
    return base + "/" + name;
}

std::string baseName(const char* path) {
    if (path == nullptr) {
        return {};
    }
    const char* slash = std::strrchr(path, '/');
    return slash == nullptr ? std::string(path) : std::string(slash + 1);
}

Status ensureDirectory(const std::string& path) {
    if (path == "/" || LittleFS.exists(path.c_str())) {
        return Status::success();
    }
    if (!LittleFS.mkdir(path.c_str())) {
        return Status::failure(StatusCode::MountFailed, "failed to create LittleFS directory");
    }
    return Status::success();
}

class LittleFsFileSystem final : public FileSystem {
public:
    Status mount(const std::string& basePath) override {
        basePath_ = normalizeBasePath(basePath);

        // Never format automatically. Users must make that decision explicitly outside PQUEUE.
        if (!LittleFS.begin(false)) {
            return Status::failure(StatusCode::MountFailed, "failed to mount LittleFS");
        }

        return ensureDirectory(basePath_);
    }

    Status readFile(const std::string& name, std::string& out) override {
        File file = LittleFS.open(path(name).c_str(), "r");
        if (!file) {
            return Status::failure(StatusCode::ReadFailed, "failed to open LittleFS file for read");
        }

        out.clear();
        while (file.available()) {
            char buffer[128];
            const std::size_t bytesRead = file.read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer));
            if (bytesRead == 0) {
                file.close();
                return Status::failure(StatusCode::ReadFailed, "failed to read LittleFS file");
            }
            out.append(buffer, bytesRead);
        }
        file.close();
        return Status::success();
    }

    Status writeFile(const std::string& name, const std::string& data) override {
        const std::string fullPath = path(name);
        File file = LittleFS.open(fullPath.c_str(), "w");
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to open LittleFS file for write");
        }
        const std::size_t bytesWritten = file.write(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        file.flush();
        file.close();
        if (bytesWritten != data.size()) {
            LittleFS.remove(fullPath.c_str());
            return Status::failure(StatusCode::WriteFailed, "failed to write complete LittleFS file");
        }
        return Status::success();
    }

    Status removeFile(const std::string& name) override {
        if (!LittleFS.remove(path(name).c_str())) {
            return Status::failure(StatusCode::RemoveFailed, "failed to remove LittleFS file");
        }
        return Status::success();
    }

    Status renameFile(const std::string& fromName, const std::string& toName) override {
        if (!LittleFS.rename(path(fromName).c_str(), path(toName).c_str())) {
            return Status::failure(StatusCode::RenameFailed, "failed to rename LittleFS file");
        }
        return Status::success();
    }

    Status listFiles(std::vector<std::string>& out) override {
        File dir = LittleFS.open(basePath_.c_str());
        if (!dir || !dir.isDirectory()) {
            if (dir) {
                dir.close();
            }
            return Status::failure(StatusCode::ListFailed, "failed to open LittleFS directory");
        }
        {
            File file = dir.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    out.push_back(baseName(file.name()));
                }
                file.close();
                file = dir.openNextFile();
            }
            dir.close();
        }
        return Status::success();
    }

    Status tryAcquireLockFile(const std::string& name) override {
        const std::string fullPath = path(name);
        if (LittleFS.exists(fullPath.c_str())) {
            return Status::failure(StatusCode::LockTimeout, "queue lock already exists");
        }

        // TODO: LittleFS create-after-exists is not a true atomic lock primitive.
        // Add owner metadata and stale-lock recovery before supporting multi-process/multi-task writers.
        File file = LittleFS.open(fullPath.c_str(), "w");
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to create LittleFS queue lock file");
        }
        file.print("locked
");
        file.flush();
        file.close();
        return Status::success();
    }

    Status releaseLockFile(const std::string& name) override {
        return removeFile(name);
    }

    std::uint64_t freeBytes() const override {
        const auto total = LittleFS.totalBytes();
        const auto used = LittleFS.usedBytes();
        return used <= total ? static_cast<std::uint64_t>(total - used) : 0;
    }

private:
    std::string path(const std::string& name) const {
        return joinPath(basePath_, name);
    }

    std::string basePath_ = "/pqueue_spool";
};

} // namespace

std::shared_ptr<FileSystem> makeLittleFsFileSystem() {
    return std::make_shared<LittleFsFileSystem>();
}

std::shared_ptr<FileSystem> makePosixFileSystem() {
    return nullptr;
}

} // namespace pqueue

#endif // ARDUINO
