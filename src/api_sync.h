#pragma once

#include "api/outbox_client.h"
#include "api/state.h"
#include "config.h"
#include "state.h"

#include <cstdint>


void syncApiState(
    const Config& config,
    const State& appState,
    api::State& apiState,
    api::ApiWriter& client
);

void syncApiState(
    const Config& config,
    const State& appState,
    api::State& apiState,
    api::ApiWriter& client,
    std::int64_t nowEpochS
);
