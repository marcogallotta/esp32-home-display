#pragma once

#include "api/client.h"
#include "api/state.h"
#include "state.h"

void syncApiState(
    const State& appState,
    api::State& apiState,
    const api::Client& client
);
