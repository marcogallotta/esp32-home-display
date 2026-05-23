#ifdef ARDUINO

#include "platform.h"

// file_log.h (and log.h) must come before any FormatLog header so that
// log.h's #ifndef LOG_LEVEL guard sets LOG_LEVEL = APP_LOG_LEVEL_INFO (2)
// before FormatLog's Settings.h can claim it as LOG_LEVEL_TRACE (5), which
// would trip log.h's range check.
#include "file_log.h"

#include <LittleFS.h>
#include <memory>

// Define FormatLog's file-storage config macros before including its headers.
// RotatingFileSink.h uses these as default template/constructor arguments, so
// they must be visible at parse time. We do NOT include FormatLog.h to avoid
// pulling in the LOG_LEVEL / LOG_FILE_LEVEL macro collision with the project.
#define LOG_FILE_MAX_BUFFER_SIZE 256
#define LOG_FILE_PATH            "/logs/app.log"
#define LOG_FILE_MAX_FILES       4
#define LOG_FILE_MAX_SIZE        8192
#define LOG_FILE_NEW_ON_BOOT     0

#include "FileStorage/FileSystem/Esp32FileManager.h"
#include "FileStorage/Sinks/RotatingFileSink.h"

namespace {

constexpr const char* kLogDir = "/logs";
constexpr const char* kLogPath = "/logs/app.log";
constexpr size_t kMaxFileSize = 8192;
constexpr size_t kMaxFiles = 4;

std::shared_ptr<fmtlog::RotatingFileSink<>> gFileSink;

} // namespace

bool initFileLogging() {
    if (!LittleFS.exists(kLogDir)) {
        if (!LittleFS.mkdir(kLogDir)) {
            return false;
        }
    }

    auto mgr = std::make_shared<fmtlog::Esp32FileManager<decltype(LittleFS)>>(LittleFS);
    gFileSink = std::make_shared<fmtlog::RotatingFileSink<>>(
        mgr,
        kLogPath,
        kMaxFiles,
        kMaxFileSize,
        /*rotateOnInit=*/false
    );
    return true;
}

void writeFileLog(LogLevel level, const std::string& msg) {
    if (!gFileSink || static_cast<int>(level) < APP_LOG_LEVEL_WARN) return;
    if (platform::isDoctorMode()) return;
    gFileSink->write(msg.c_str(), msg.size());
    gFileSink->write("\n", 1);
}

void logLifecycleEvent(const std::string& msg) {
    if (!gFileSink) {
        return;
    }
    const std::string line = "[" + logTimestamp() + "] [INFO] " + msg + "\n";
    gFileSink->write(line.c_str(), line.size());
}

#endif // ARDUINO
