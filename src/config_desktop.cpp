#ifndef ARDUINO
#include "config.h"

#include <fstream>
#include <sstream>

bool loadConfig(Config& config) {
    std::ifstream fin("config.json");
    if (!fin) {
        return false;
    }

    std::stringstream buffer;
    buffer << fin.rdbuf();
    return parseConfigText(buffer.str(), config);
}
#endif
