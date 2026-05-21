#pragma once

#include "../network.h"

namespace api {

enum class BackendWriteResult {
    Failed,
    Created,
    Duplicate,
    Merged,
    MergedWithConflict,
    Conflict,
};

BackendWriteResult parseBackendWriteResult(const network::HttpResponse& response);

} // namespace api
