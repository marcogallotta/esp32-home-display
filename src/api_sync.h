#pragma once

#include "api/buffered_client.h"
#include "api/state.h"
#include "config.h"
#include "state.h"


void syncApiState(
    const Config& config,
    const State& appState,
    api::State& apiState,
    api::BufferedClient& client
);
