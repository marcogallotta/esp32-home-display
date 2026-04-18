#pragma once

#include "types.h"

namespace salah {

struct State {
    Phase current;
    Phase next;
    int minutesRemaining;
};

// Pure logic: no IO, no library deps
State computeState(const Schedule& today,
                   const Schedule& tomorrow,
                   int nowMinutes);

} // namespace salah
