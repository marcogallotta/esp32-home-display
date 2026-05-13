#pragma once

// Only available on device. Desktop builds use serial-only logging.
#ifdef ARDUINO

#include <string>

#include "log.h"

// Initialise rotating file logging under /logs.
// Must be called after LittleFS is mounted (i.e. after loadConfig() succeeds).
// Returns false if setup fails; serial logging continues regardless.
bool initFileLogging();

// Write msg to the file log. Only WARN and above are persisted.
void writeFileLog(LogLevel level, const std::string& msg);

// Explicit hook for lifecycle INFO events that should be persisted to file.
// Not wired to logLine() — call directly for boot/shutdown/recovery milestones.
void logLifecycleEvent(const std::string& msg);

#endif // ARDUINO
