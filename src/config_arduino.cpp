#ifdef ARDUINO
#include "config.h"
#include "platform.h"

#include <LittleFS.h>

namespace {

bool readTextFile(const std::string& path, std::string& out) {
    File fin = LittleFS.open(path.c_str(), "r");
    if (!fin) {
        return false;
    }

    out.resize(fin.size());
    const size_t n = fin.readBytes(out.data(), out.size());
    out.resize(n);
    return true;
}

bool loadPemFiles(Config& config) {
    config.api.pem.clear();
    return readTextFile(config.forecast.openmeteoPemFile, config.forecast.openmeteoPem) &&
           readTextFile(config.api.pemFile, config.api.pem);
}

} // namespace

bool loadConfig(Config& config) {
    if (!LittleFS.begin(false)) {
        return false;
    }

    std::string text;
    if (!readTextFile("/config.json", text)) {
        return false;
    }

    if (!parseConfigText(text, config)) {
        return false;
    }

    return loadPemFiles(config);
}
#endif
