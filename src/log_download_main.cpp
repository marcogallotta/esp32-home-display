#ifdef ARDUINO

#include <Arduino.h>
#include <LittleFS.h>

#include <string>

// Dumps all retained log files to serial in chronological order (oldest first).
// Flash file rotation naming: app.4.log ... app.1.log, then app.log (current).

namespace {

constexpr const char* kLogFiles[] = {
    "/logs/app.4.log",
    "/logs/app.3.log",
    "/logs/app.2.log",
    "/logs/app.1.log",
    "/logs/app.log",
};

void dumpFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        return;
    }

    Serial.print("--- ");
    Serial.print(path);
    Serial.print(" (");
    Serial.print(f.size());
    Serial.println(" bytes) ---");

    uint8_t buf[256];
    while (f.available()) {
        const int n = f.read(buf, sizeof(buf));
        if (n > 0) {
            Serial.write(buf, n);
        }
    }
    f.close();

    Serial.println();
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("log_download: start");

    if (!LittleFS.begin(false)) {
        Serial.println("log_download: littlefs_mount_failed");
        return;
    }

    Serial.print("log_download: total_bytes=");
    Serial.print(LittleFS.totalBytes());
    Serial.print(" used_bytes=");
    Serial.println(LittleFS.usedBytes());

    bool any = false;
    for (const char* path : kLogFiles) {
        if (LittleFS.exists(path)) {
            dumpFile(path);
            any = true;
        }
    }

    if (!any) {
        Serial.println("log_download: no_log_files_found");
    }

    Serial.println("log_download: done");
}

void loop() {}

#endif // ARDUINO
