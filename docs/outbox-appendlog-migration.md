# Outbox Migration: Fixed-Slot to AppendLog

**Editing rules:** ASCII only -- no Unicode symbols. This file is a living reference; delete completed items, rewrite state sections, never mark things done inline.

This document covers the app-side steps to migrate the API outbox from the
pqueue fixed-slot backend to the append-log backend. The pqueue-side
prerequisites and the final fixed-slot elimination are in
`pqueue/docs/pqueue-fixed-slot-removal.md`.

**Prerequisite:** `docs/compactidle-integration.md` must be complete before
starting this migration. Steps here assume `api::OutboxClient::compactIdle`
already exists and the main-loop call is already wired.

---

## Overview

The outbox currently uses `pqueue::StoreLayout::FixedSlot` (the default) and
writes to `/pqueue_api_spool`. The migration switches it to `StoreLayout::AppendLog`
adds idle compaction to the main loop, and adds one new config field. No on-disk format conversion is attempted; deployed devices should drain or
explicitly abandon the old fixed-slot backlog before switching. The two backends
write separate filenames and never conflict.

---

## Steps

### 1. Add idleCompactSteps to OutboxConfig

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

### 2. Switch the queue config in outbox_client.cpp

In `makeHttpConfig`, add:

```cpp
httpConfig.queue.storeLayout = pqueue::StoreLayout::AppendLog;
```

AppendLog-specific tuning fields (`maxSegmentBytes`, `maxSegments`, `minFreeBytes`)
are left at their library defaults unless profiling shows a reason to change them.

### 3. Tests

Extend `tests/config.cpp`:

- `idle_compact_steps` parses correctly; missing = default 1; negative value
  returns parse error.

Extend `tests/api_outbox_client.cpp`:

- After enqueueing a record with an injected filesystem, the queue directory
  contains `mf-a.bin` and a `seg-*.bin`, not `pqueue.spool`.
- `compactIdle` on an empty store returns ok and zero compactions.
- Existing drain-rate tests pass unchanged.

### 4. One-time cutover

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
