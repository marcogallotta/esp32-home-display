#include "backend_result.h"

#include <ArduinoJson.h>

namespace api {

BackendWriteResult parseBackendWriteResult(const network::HttpResponse& response) {
    StaticJsonDocument<1024> doc;
    const DeserializationError err = deserializeJson(doc, response.body);
    if (err) {
        return BackendWriteResult::Failed;
    }

    const char* status = doc["status"];
    const char* result = doc["result"];
    if (status == nullptr || result == nullptr) {
        return BackendWriteResult::Failed;
    }

    if (std::string(status) != "ok") {
        return BackendWriteResult::Failed;
    }

    const std::string resultStr(result);
    if (resultStr == "created") {
        return BackendWriteResult::Created;
    }
    if (resultStr == "duplicate") {
        return BackendWriteResult::Duplicate;
    }
    if (resultStr == "merged") {
        return BackendWriteResult::Merged;
    }
    if (resultStr == "conflict") {
        return BackendWriteResult::Conflict;
    }

    return BackendWriteResult::Failed;
}

} // namespace api
