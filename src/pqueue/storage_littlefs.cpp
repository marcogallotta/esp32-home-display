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


    Status readAt(const std::string& name, std::uint64_t offset, std::size_t size, std::string& out) override {
        File file = LittleFS.open(path(name).c_str(), "r");
        if (!file) {
            return Status::failure(StatusCode::ReadFailed, "failed to open LittleFS file for positioned read");
        }
        if (!file.seek(static_cast<std::uint32_t>(offset), SeekSet)) {
            file.close();
            return Status::failure(StatusCode::ReadFailed, "failed to seek LittleFS file for read");
        }
        out.assign(size, '\0');
        const std::size_t bytesRead = file.read(reinterpret_cast<std::uint8_t*>(out.data()), size);
        file.close();
        if (bytesRead != size) {
            return Status::failure(StatusCode::ReadFailed, "failed to read complete LittleFS range");
        }
        return Status::success();
    }

    Status writeAt(const std::string& name, std::uint64_t offset, const std::string& data) override {
        File file = LittleFS.open(path(name).c_str(), "r+");
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to open LittleFS file for positioned write");
        }
        if (!file.seek(static_cast<std::uint32_t>(offset), SeekSet)) {
            file.close();
            return Status::failure(StatusCode::WriteFailed, "failed to seek LittleFS file for write");
        }
        const std::size_t bytesWritten = file.write(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
        file.flush();
        file.close();
        if (bytesWritten != data.size()) {
            return Status::failure(StatusCode::WriteFailed, "failed to write complete LittleFS range");
        }
        return Status::success();
    }

    Status resizeFile(const std::string& name, std::uint64_t size) override {
        const std::string fullPath = path(name);
        File file = LittleFS.open(fullPath.c_str(), "r+");
        if (!file) {
            file = LittleFS.open(fullPath.c_str(), "w");
        }
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to open LittleFS file for resize");
        }
        const std::uint64_t current = file.size();
        if (current > size) {
            file.close();
            LittleFS.remove(fullPath.c_str());
            file = LittleFS.open(fullPath.c_str(), "w");
            if (!file) {
                return Status::failure(StatusCode::WriteFailed, "failed to recreate LittleFS file for resize");
            }
        } else if (current < size) {
            if (!file.seek(static_cast<std::uint32_t>(current), SeekSet)) {
                file.close();
                return Status::failure(StatusCode::WriteFailed, "failed to seek LittleFS file for resize");
            }
        }
        char zeros[128] = {};
        std::uint64_t written = current > size ? 0 : current;
        while (written < size) {
            const std::uint64_t remaining = size - written;
            const std::size_t chunk = remaining > sizeof(zeros) ? sizeof(zeros) : static_cast<std::size_t>(remaining);
            if (file.write(reinterpret_cast<const std::uint8_t*>(zeros), chunk) != chunk) {
                file.close();
                return Status::failure(StatusCode::WriteFailed, "failed to size LittleFS file");
            }
            written += chunk;
        }
        file.flush();
        file.close();
        return Status::success();
    }

    Status fileSize(const std::string& name, std::uint64_t& out) override {
        File file = LittleFS.open(path(name).c_str(), "r");
        if (!file) {
            return Status::failure(StatusCode::ReadFailed, "failed to stat LittleFS file");
        }
        out = static_cast<std::uint64_t>(file.size());
        file.close();
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

    Status tryAcquireLockFile(const std::string& name, const std::string& contents) override {
        const std::string fullPath = path(name);
        if (LittleFS.exists(fullPath.c_str())) {
            return Status::failure(StatusCode::LockTimeout, "queue lock already exists");
        }

        // TODO: LittleFS create-after-exists is not a true atomic lock primitive.
        // POSIX can recover stale pid locks; ESP32 needs a future boot-id/age/force recovery policy.
        File file = LittleFS.open(fullPath.c_str(), "w");
        if (!file) {
            return Status::failure(StatusCode::WriteFailed, "failed to create LittleFS queue lock file");
        }
        file.write(reinterpret_cast<const uint8_t*>(contents.data()), contents.size());
        file.flush();
        file.close();
        return Status::success();
    }

    Status releaseLockFile(const std::string& name, const std::string& expectedContents) override {
        std::string actual;
        Status st = readFile(name, actual);
        if (!st.ok()) {
            return st;
        }
        if (actual != expectedContents) {
            return Status::failure(StatusCode::LockTimeout, "queue lock is owned by another process");
        }
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
