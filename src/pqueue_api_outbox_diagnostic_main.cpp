#ifdef ARDUINO

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "api/types.h"
#include "pqueue/diagnostics.h"
#include "pqueue/types.h"

namespace {

constexpr const char* kConfigPath = "/config.json";
constexpr const char* kApiQueueBasePath = "/pqueue_api_spool";

struct ApiDiagnosticConfig {
    std::string baseUrl;
    std::string pemFile;
    std::uint32_t diskReserveBytes = api::OutboxConfig{}.diskReserveBytes;
};

void printLine(const std::string& line) {
    Serial.println(line.c_str());
}

std::string yesNo(bool value) {
    return value ? "yes" : "no";
}

std::string statusSummary(const pqueue::Status& status) {
    std::string out = status.ok() ? "ok" : "failed";
    out += " code=";
    out += pqueue::statusCodeName(status.code);
    if (status.backendCode != 0) {
        out += " backend=";
        out += std::to_string(status.backendCode);
    }
    if (status.message != nullptr && status.message[0] != '\0') {
        out += " message=\"";
        out += status.message;
        out += "\"";
    }
    return out;
}

std::string sanitizeLine(std::string value) {
    for (char& c : value) {
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) {
            c = '?';
        }
    }
    return value;
}


bool littleFsPathExists(const char* path) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    file.close();
    return true;
}

bool readTextFile(const char* path, std::string& out) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    out.clear();
    while (file.available()) {
        char buffer[128];
        const std::size_t n = file.read(reinterpret_cast<std::uint8_t*>(buffer), sizeof(buffer));
        if (n == 0) {
            file.close();
            return false;
        }
        out.append(buffer, n);
    }
    file.close();
    return true;
}

bool readFirstBytes(const char* path, std::size_t maxBytes, std::string& out, std::uint64_t& fileSize) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    fileSize = file.size();
    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(maxBytes, fileSize));
    out.assign(n, '\0');
    const std::size_t read = n == 0 ? 0 : file.read(reinterpret_cast<std::uint8_t*>(out.data()), n);
    out.resize(read);
    file.close();
    return read == n;
}

std::string firstLine(const std::string& text) {
    const auto end = text.find('\n');
    std::string line = end == std::string::npos ? text : text.substr(0, end);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() > 96) {
        line.resize(96);
    }
    return sanitizeLine(line);
}

bool startsWith(const std::string& value, const char* prefix) {
    const std::string p(prefix);
    return value.size() >= p.size() && value.compare(0, p.size(), p) == 0;
}

bool loadApiDiagnosticConfig(ApiDiagnosticConfig& out) {
    std::string text;
    if (!readTextFile(kConfigPath, text)) {
        printLine("config: missing_or_unreadable path=/config.json");
        return false;
    }

    StaticJsonDocument<4096> json;
    const DeserializationError err = deserializeJson(json, text.c_str(), text.size());
    if (err) {
        printLine(std::string("config: parse_failed error=") + err.c_str());
        return false;
    }

    const JsonObject api = json["api"].as<JsonObject>();
    if (api.isNull()) {
        printLine("config: api object missing");
        return false;
    }

    out.baseUrl = api["base_url"] | "";
    out.pemFile = api["pem_file"] | "";

    const JsonObject outbox = api["outbox"].isNull()
        ? api["buffer"].as<JsonObject>()
        : api["outbox"].as<JsonObject>();
    if (!outbox.isNull() && !outbox["disk_reserve_bytes"].isNull()) {
        out.diskReserveBytes = outbox["disk_reserve_bytes"].as<std::uint32_t>();
    }

    printLine(
        "config: loaded base_url=\"" + out.baseUrl +
        "\" pem_file=\"" + out.pemFile +
        "\" disk_reserve_bytes=" + std::to_string(out.diskReserveBytes)
    );
    return true;
}

void printPqueueDiagnostic(const ApiDiagnosticConfig& config) {
    pqueue::FileStoreConfig queueConfig;
    queueConfig.basePath = kApiQueueBasePath;
    queueConfig.backend = pqueue::StorageBackend::LittleFS;
    queueConfig.reservedBytes = config.diskReserveBytes;
    queueConfig.recordSizeBytes = pqueue::Config{}.recordSizeBytes;

    const pqueue::FileStoreDiagnostic diag = pqueue::diagnoseFileStore(queueConfig, 192);

    printLine("pqueue_diag: base_path=" + diag.basePath + " mount=" + statusSummary(diag.mountStatus));
    if (!diag.mountStatus.ok()) {
        return;
    }

    printLine(
        "pqueue_diag: littlefs_free_bytes=" + std::to_string(diag.freeBytes) +
        " list=" + statusSummary(diag.listStatus)
    );

    std::string files = "pqueue_diag: files=";
    if (diag.files.empty()) {
        files += "<none>";
    } else {
        for (std::size_t i = 0; i < diag.files.size(); ++i) {
            if (i != 0) {
                files += ",";
            }
            files += diag.files[i];
        }
    }
    printLine(files);

    printLine(
        "pqueue_diag: layout_valid=" + yesNo(diag.layout.valid) +
        " capacity_records=" + std::to_string(diag.layout.capacityRecords) +
        " record_size=" + std::to_string(diag.layout.recordSizeBytes) +
        " reserved_bytes=" + std::to_string(diag.layout.reservedBytes) +
        " journal_bytes=" + std::to_string(diag.layout.journalBytes) +
        " expected_spool_bytes=" + std::to_string(diag.layout.spoolBytes)
    );

    printLine(
        "pqueue_diag: spool_exists=" + yesNo(diag.spoolExists) +
        " spool_listed=" + yesNo(diag.spoolListed) +
        " spool_size_status=" + statusSummary(diag.spoolSizeStatus) +
        " actual_spool_bytes=" + std::to_string(diag.spoolSizeBytes) +
        " size_matches=" + yesNo(diag.spoolSizeMatches)
    );

    printLine(
        "pqueue_diag: legacy_dot_lock_listed=" + yesNo(diag.legacyDotLockListed) +
        " legacy_named_lock_listed=" + yesNo(diag.legacyNamedLockListed) +
        " legacy_dot_lock_path_exists=" + yesNo(littleFsPathExists("/pqueue_api_spool/.pqueue.lock")) +
        " legacy_dot_lock_owner_exists=" + yesNo(littleFsPathExists("/pqueue_api_spool/.pqueue.lock/owner")) +
        " legacy_named_lock_path_exists=" + yesNo(littleFsPathExists("/pqueue_api_spool/pqueue.lock")) +
        " legacy_named_lock_owner_exists=" + yesNo(littleFsPathExists("/pqueue_api_spool/pqueue.lock/owner"))
    );

    for (const auto& slot : diag.checkpointSlots) {
        printLine(
            "pqueue_diag: checkpoint_slot=" + std::to_string(slot.slot) +
            " state=" + pqueue::checkpointSlotStateName(slot.state) +
            " gen=" + std::to_string(slot.generation) +
            " head=" + std::to_string(slot.head) +
            " tail=" + std::to_string(slot.tail) +
            " count=" + std::to_string(slot.count) +
            " cap=" + std::to_string(slot.capacityRecords) +
            " record_size=" + std::to_string(slot.recordSizeBytes) +
            " reserved=" + std::to_string(slot.reservedBytes) +
            " journal=" + std::to_string(slot.journalBytes) +
            " journal_used=" + std::to_string(slot.journalUsedBytes)
        );
    }

    printLine(
        "pqueue_diag: usable_checkpoint=" + yesNo(diag.hasUsableCheckpoint) +
        " config_mismatch=" + yesNo(diag.hasConfigMismatch)
    );

    if (!diag.firstBytesHex.empty()) {
        printLine("pqueue_diag: first_bytes_hex=" + diag.firstBytesHex);
    }
}

void printPemDiagnostic(const ApiDiagnosticConfig& config) {
    if (config.pemFile.empty()) {
        printLine("api_pem_diag: path=<empty> exists=no valid_header=no");
        return;
    }

    std::string prefix;
    std::uint64_t size = 0;
    const bool exists = readFirstBytes(config.pemFile.c_str(), 256, prefix, size);
    const std::string line = firstLine(prefix);
    const bool validHeader = startsWith(line, "-----BEGIN CERTIFICATE-----");
    const bool escapedNewlines = prefix.find("\\n") != std::string::npos;
    const bool privateKey = prefix.find("PRIVATE KEY") != std::string::npos;

    printLine(
        "api_pem_diag: path=\"" + config.pemFile +
        "\" exists=" + yesNo(exists) +
        " size=" + std::to_string(size) +
        " valid_header=" + yesNo(validHeader) +
        " escaped_newlines=" + yesNo(escapedNewlines) +
        " contains_private_key=" + yesNo(privateKey) +
        " first_line=\"" + line + "\""
    );
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(1500);

    printLine("api_outbox_diagnostic: start");

    if (!LittleFS.begin(false)) {
        printLine("littlefs: mount_failed");
        return;
    }

    printLine(
        "littlefs: mounted total_bytes=" + std::to_string(LittleFS.totalBytes()) +
        " used_bytes=" + std::to_string(LittleFS.usedBytes())
    );

    ApiDiagnosticConfig config;
    loadApiDiagnosticConfig(config);
    printPqueueDiagnostic(config);
    printPemDiagnostic(config);

    printLine("api_outbox_diagnostic: done");
}

void loop() {}

#endif // ARDUINO
