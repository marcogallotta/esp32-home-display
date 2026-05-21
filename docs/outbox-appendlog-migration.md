# Outbox Migration: Fixed-Slot to AppendLog

**Editing rules:** ASCII only -- no Unicode symbols. This file is a living reference; delete completed items, rewrite state sections, never mark things done inline.

This document covers the app-side steps to migrate the API outbox from the
pqueue fixed-slot backend to the append-log backend. The pqueue-side
prerequisites and the final fixed-slot elimination are in
`pqueue/docs/pqueue-fixed-slot-removal.md`.

**Prerequisite:** `pqueue/docs/compactidle-integration.md` must be complete before
starting this migration (`pqueue::Outbox` and `pqueue::http::Outbox` wrappers exist).

---

## Overview

The outbox currently uses `pqueue::StoreLayout::FixedSlot` (the default) and
writes to `/pqueue_api_spool`. The migration switches it to `StoreLayout::AppendLog`,
adds idle compaction to the main loop, and adds one new config field. No on-disk format conversion is attempted; deployed devices should drain or
explicitly abandon the old fixed-slot backlog before switching. The two backends
write separate filenames and never conflict.

---

## Steps

### 1. Expose compactIdle in api::OutboxClient

Add to `api::OutboxClient` in `src/api/outbox_client.h`:

```cpp
pqueue::CompactIdleResult compactIdle(size_t maxSteps);
```

Implement in `outbox_client.cpp` as a passthrough to `pqueue_->outbox.compactIdle(maxSteps)`.

### 2. Wire compactIdle into syncOutputs

In `src/main.cpp` `syncOutputs()`, after `drainPending` and before `syncApiState`:

```cpp
if (config.api.outbox.idleCompactSteps > 0) {
    const auto cr = app.apiOutboxClient.compactIdle(
        static_cast<size_t>(config.api.outbox.idleCompactSteps));
    if (cr.compactions > 0 || !cr.status.ok()) {
        logLine(LogLevel::Info,
            "pqueue idle compaction: steps=" + std::to_string(cr.stepsRun) +
            " compactions=" + std::to_string(cr.compactions) +
            (cr.status.ok() ? "" : " error=1"));
    }
}
```

One bounded call per tick, after drain and before new writes. Drain creates dead
bytes; compactIdle reclaims them before the next enqueue burst. Do not run an
unbounded loop here; heavy compaction steps can be multi-second on device.

### 3. Add idleCompactSteps to OutboxConfig

Add one field to `api::OutboxConfig` in `src/api/types.h`:

```cpp
int idleCompactSteps = 1;
```

Parse it in `src/config.cpp` under the `api.outbox` section, following the
write-at-end pattern. Validation: must be >= 0. Zero disables idle compaction
without removing the call site.

No other new config fields. `maxSegmentBytes`, `maxSegments`, and `minFreeBytes`
use the pqueue library defaults (4096 / 16 / 32768) and are hardcoded in
`outbox_client.cpp` if they ever need to differ from those defaults.
`diskReserveBytes` already maps to `maxTotalBytes`.

### 4. Switch the queue config in outbox_client.cpp

In `makeHttpConfig`, add:

```cpp
httpConfig.queue.storeLayout = pqueue::StoreLayout::AppendLog;
```

AppendLog-specific tuning fields (`maxSegmentBytes`, `maxSegments`, `minFreeBytes`)
are left at their library defaults unless profiling shows a reason to change them.

### 5. Tests

Extend `tests/config.cpp`:

- `idle_compact_steps` parses correctly; missing = default 1; negative value
  returns parse error.

Extend `tests/api_outbox_client.cpp`:

- After enqueueing a record with an injected filesystem, the queue directory
  contains `mf-a.bin` and a `seg-*.bin`, not `pqueue.spool`.
- `compactIdle` on an empty store returns ok and zero compactions.
- Existing drain-rate tests pass unchanged.

### 6. One-time cutover

Run `uploadfs` to reformat LittleFS on the device, then delete any local posix
spools:

```
rm -rf ~/esp32-home-display*/build/pqueue-spools/
```

---

## What does not change

- `fullQueuePolicy = DropOldest` -- data-loss behaviour is a separate decision.
- `drainRateCap`, `retryDelayMs`, `logLevel` -- unchanged.
- The `ApiWriter` interface and all `postFooReading` methods -- unchanged.
- Drain semantics in `drainPending()` -- unchanged.
