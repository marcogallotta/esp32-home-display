#pragma once

#include <vector>

#include "../state.h"

struct DirtyRegions {
    bool salahName = false;
    bool minutes = false;
    bool sensorsAny = false;
    std::vector<bool> sensorRows;
    bool forecast = false;
};

struct UiState {
    DirtyRegions dirty;
};

int displayTemp(float x);
DirtyRegions computeDirtyRegions(const State& previous, const State& current);
