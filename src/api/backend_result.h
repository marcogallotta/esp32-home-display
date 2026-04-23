#pragma once

#include "../network.h"

namespace api {

enum class BackendWriteResult {
    Failed,
    Created,
    Duplicate,
    Merged,
    Conflict,
};

BackendWriteResult parseBackendWriteResult(const network::HttpResponse& response);

} // namespace api
