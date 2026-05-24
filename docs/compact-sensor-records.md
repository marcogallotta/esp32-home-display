# Compact Sensor Records

Reduce pqueue flash usage by storing binary sensor records on disk instead of
full JSON HTTP bodies. JSON is reconstructed from binary at drain time, just
before the HTTP send.

---

## Motivation

The pqueue currently stores the complete HTTP request body verbatim. A typical
SwitchBot record looks like:

```json
{"mac":"AA:BB:CC:DD:EE:FF","name":"Living Room","type":"switchbot","timestamp":"2026-05-24T12:34:56Z","temperature_c":23.4,"humidity_pct":61}
```

Full on-disk cost per record breakdown:

```
pqueue event overhead:  24 B
envelope header (v2):   13 B
path (/switchbot/reading): 18 B
JSON body:             ~130 B
----------------------------------
total:                 ~185 B
```

With binary compact encoding the same record becomes ~81 B on disk — a **~2.3×
reduction**. At the current `disk_reserve_bytes = 262144` (256 KB) this raises
worst-case buffering capacity from ~1 400 records to ~3 200 records before
`QueueFull` drops are hit.

---

## Is the pqueue API change breaking?

**No — purely additive.**

- `submitPost(path, string)` is untouched.
- `submitCompactPost(path, Span)` is a new overload.
- `http::Config` gets an optional `expandBody` callback; existing callers leave
  it null and behaviour is identical.
- `RequestEnvelope` version 2 is a new format; existing version-1 records on
  disk decode unchanged with `encoding = Raw`.

**Downgrade note:** new firmware can drain old v1 records without issue.
Because `encodeRequestEnvelope` always writes version 2, all records written
by new firmware — Raw or Compact — are v2. Old firmware will reject them on
version mismatch. Downgrading after any new records have been written to the
spool is unsupported; wipe the spool on downgrade.

---

## pqueue changes (library layer)

### 1. `RequestEnvelope` — new `BodyEncoding` field

```cpp
// pqueue/src/pqueue/http/request_envelope.h

enum class BodyEncoding : std::uint8_t {
    Raw     = 0,  // body is the HTTP request body (existing behaviour)
    Compact = 1,  // body is a compact binary record; expand before sending
};

struct RequestEnvelope {
    Method method = Method::Post;
    BodyEncoding encoding = BodyEncoding::Raw;  // version-1 records decode as Raw
    std::string path;
    std::string body;
};
```

The existing version-1 format is unchanged. Version 2 adds one byte after the
method byte for `encoding`. `decodeRequestEnvelope` handles both versions:
version 1 → `encoding = Raw`, version 2 → reads the `encoding` byte.
`encodeRequestEnvelope` always writes version 2.

Old v1 decoders will see `version = 2` and reject it (version mismatch), which
is the correct safe failure — they must not attempt to decode a format they
do not understand. Unknown `encoding` values must be rejected at decode time,
not deferred to the drain loop, so a corrupt or future-format record never
reaches the HTTP send path.

### 2. `http::Config` — expand callback

The callback uses an output-parameter style so failure is unambiguous and an
empty expanded body (pathological but possible) does not look like failure.

```cpp
// pqueue/src/pqueue/http/outbox.h

using ExpandBodyCallback = bool (*)(
    const char* path,
    const std::uint8_t* compactBody,
    std::size_t compactSize,
    void* context,
    std::string& out      // populated on success
);

struct Config {
    // ...existing fields unchanged...
    ExpandBodyCallback expandBody = nullptr;
    void* expandBodyContext = nullptr;
};
```

### 3. `http::Outbox` — new submission overload

```cpp
// stores record with encoding = Compact
SubmitResult submitCompactPost(const std::string& path, pqueue::Span record);
```

`submitCompactPost` must copy the span bytes synchronously before returning.
Callers typically pass a span over a local `std::vector`; the vector may be
destroyed immediately after the call returns.

### 4. Drain path — expand before send

In the drain loop, after `decodeRequestEnvelope`, before constructing the HTTP
request:

```cpp
std::string httpBody;
if (envelope.encoding == BodyEncoding::Compact) {
    if (config_.expandBody == nullptr) {
        drop(DropReason::DecodeFailed);
        continue;
    }
    if (!config_.expandBody(
            envelope.path.c_str(),
            reinterpret_cast<const std::uint8_t*>(envelope.body.data()),
            envelope.body.size(),
            config_.expandBodyContext,
            httpBody)) {
        drop(DropReason::DecodeFailed);
        continue;
    }
} else {
    httpBody = envelope.body;
}
```

A missing callback with a Compact record is always a drop, never a raw send.
Use `DropReason::ConfigError` or `DropReason::ExpandFailed` if those exist in
the library, as they are more diagnostic than `DecodeFailed`; fall back to
`DecodeFailed` if they do not.
Unknown `encoding` values are caught earlier, at `decodeRequestEnvelope`, so
they never reach this branch.

---

## App changes (outbox_client.cpp / payloads layer)

### Compact record binary format

All multi-byte integers are little-endian.

```
[1]  version = 1
[1]  type: 0x01=switchbot  0x02=xiaomi_temp  0x03=xiaomi_moisture
          0x04=xiaomi_lux  0x05=xiaomi_conductivity
[6]  mac: raw 6 bytes (no colons)
[4]  epoch_s: uint32
[1]  name_len: uint8
[N]  name: name_len bytes (UTF-8, no null terminator)
[fields per type — see below]
```

**SwitchBot (0x01):**
```
[2]  temp_x10: int16   (°C × 10, e.g. 23.4 → 234)
[1]  humidity_pct: uint8
```
Total: 16 + name_len bytes. With a 10-char name: **26 bytes**.

**Xiaomi temperature (0x02):**
```
[2]  temp_x10: int16
```
Total: 15 + name_len bytes. With a 10-char name: **25 bytes**.

**Xiaomi moisture (0x03):**
```
[1]  moisture_pct: uint8
```
Total: 14 + name_len bytes.

**Xiaomi lux (0x04):**
```
[4]  lux: uint32
```
Total: 17 + name_len bytes.

**Xiaomi conductivity (0x05):**
```
[2]  conductivity: uint16
```
Total: 15 + name_len bytes.

### Encode / expand functions

Add to `src/api/payloads.h`:

```cpp
// Returns an empty vector on failure.
std::vector<std::uint8_t> encodeCompact(const SwitchbotPayload& payload);
std::vector<std::uint8_t> encodeCompact(const XiaomiPayload& payload);

// Reconstructs JSON from a compact binary record.
// Returns true and populates `out` on success; returns false on any decode error.
bool expandCompact(const char* path,
                   const std::uint8_t* data, std::size_t size,
                   std::string& out);
```

`expandCompact` matches the `ExpandBodyCallback` signature and is registered
directly as `config.expandBody` in `OutboxClientImpl`. It reconstructs JSON
using ArduinoJson identically to `toJson()`.

### OutboxClient call sites

`postSwitchbotReading` and `postXiaomiReading` replace
`submitPost(path, toJson(...))` with:

```cpp
const auto compact = encodeCompact(*payload);
if (compact.empty()) {
    // encode failure — treat as invalid payload, drop
    return makeWriteResult(WriteStatus::DroppedPermanent);
}
return pqueue_->outbox.submitCompactPost(
    path,
    pqueue::Span{compact.data(), compact.size()}
);
```

Do not route through `ApiRequest` / `send()`; those paths call `submitPost`
which stores `encoding = Raw`.

---

## On-disk size comparison

Per-record cost = pqueue event overhead (24 B) + envelope header v2 (13 B) +
path (approximate — varies by endpoint) + body. Compact body assumes 10-char
sensor name.

| Record type        | JSON body | JSON total | Compact body | Compact total | Savings |
|--------------------|-----------|------------|--------------|---------------|---------|
| SwitchBot          | ~130 B    | ~185 B     | 26 B         | ~81 B         | ~2.3×   |
| Xiaomi temperature | ~120 B    | ~175 B     | 25 B         | ~80 B         | ~2.2×   |
| Xiaomi lux         | ~125 B    | ~180 B     | 27 B         | ~82 B         | ~2.2×   |

---

## `disk_reserve_bytes` recommendation

Current: **256 KB**, ~1 400 SwitchBot records at ~185 B each.

After this change, 256 KB holds **~3 200 SwitchBot records** at ~81 B each.
Keep at 256 KB — the extra headroom is useful for sustained outages and costs
nothing. Only reduce if the device is genuinely flash-constrained for other
reasons: 128 KB still gives ~1 600 records, slightly better than today's
256 KB JSON capacity, and frees 128 KB for other LittleFS use.

LittleFS total: 1 920 KB. 256 KB reservation = 13%. 128 KB = 6.5%.
Adjust via `config.json` without a firmware change.

---

## Migration

No migration needed. The queue drains in normal operation and should be empty
at upgrade time. Old v1 envelopes on a device updated mid-outage drain
correctly (decoded as `encoding = Raw`, body sent as-is).

Downgrading after compact records have been written is unsupported — wipe the
spool on downgrade.

---

## Required tests

**Envelope decode (pqueue layer):**
- v1 envelope decodes as `encoding = Raw`
- v2 envelope with `Raw` decodes correctly
- v2 envelope with `Compact` decodes correctly
- Unknown `encoding` value → `decodeRequestEnvelope` returns false
- v1 decoder rejects a v2 record (version mismatch → decode failure, not silent misparse)

**Drain path (pqueue layer):**
- Compact record + missing `expandBody` callback → drop, never sends binary
- Corrupt compact payload → `expandCompact` returns false → drop

**App layer:**
- Old v1 (Raw) records drain correctly after firmware upgrade to v2

---

## Separation of concerns

`submitCompactPost` and `expandBody` are generic pqueue features — they know
nothing about sensor types, JSON, or ArduinoJson. `encodeCompact` and
`expandCompact` belong entirely in the app's payload layer (`src/api/payloads`)
and must not leak into pqueue. The pqueue layer treats the compact body as
opaque bytes.

---

## Out of scope

- Compact records for the dropped-log spool (separate spool, separate design).
- Per-sensor name table at expand time instead of inline storage. Inline is
  simpler and the byte savings are negligible.
