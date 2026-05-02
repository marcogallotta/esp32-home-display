#pragma once

#include "api/outbox_client.h"
#include "api/state.h"
#include "config.h"
#include "state.h"


void syncApiState(
    const Config& config,
    const State& appState,
    api::State& apiState,
    api::OutboxClient& client
);
