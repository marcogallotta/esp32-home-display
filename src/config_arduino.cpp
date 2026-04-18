#ifdef ARDUINO
#include "config.h"
#include "platform.h"

#include <SPIFFS.h>

bool loadConfig(Config& config) {
    if (!SPIFFS.begin(false)) {
        return false;
    }

    File fin = SPIFFS.open("/config.json", "r");
    if (!fin) {
        return false;
    }

    std::string text(fin.size(), '\0');
    size_t n = fin.readBytes(text.data(), text.size());
    text.resize(n);

    return parseConfigText(text, config);
}
#endif
