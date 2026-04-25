#ifndef ARDUINO
#include "config.h"

#include <fstream>
#include <sstream>
#include <string>

namespace {

bool readTextFile(const std::string& path, std::string& out) {
    std::string resolved = path;
    if (!resolved.empty() && resolved.front() == '/') {
        resolved.erase(0, 1);
    }

    std::ifstream fin("data/" + resolved);
    if (!fin) {
        return false;
    }

    std::stringstream buffer;
    buffer << fin.rdbuf();
    out = buffer.str();
    return true;
}

bool loadPemFiles(Config& config) {
    return readTextFile(config.api.pemFile, config.api.pem) &&
           readTextFile(config.forecast.openmeteoPemFile, config.forecast.openmeteoPem);
}

} // namespace

bool loadConfig(Config& config) {
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
