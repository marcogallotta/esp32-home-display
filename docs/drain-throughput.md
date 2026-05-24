# Drain throughput improvements

Two sequential steps: pqueue connection reuse first, then add firmware-side timing logs to measure the result.

---

## Step 1: pqueue - persistent TLS connection in Esp32ArduinoTransport

**File:** `pqueue/src/pqueue/http/esp32_arduino_transport.h` / `.cpp`

Currently `post()` allocates a stack-local `WiFiClientSecure` and `HTTPClient` on every call, which forces a full TLS handshake per drain attempt. The fix is to make `WiFiClientSecure` and the CA cert string persistent members so the TCP/TLS socket survives across calls.

### Header changes

The header is already `#ifdef ARDUINO` guarded so adding `#include <WiFiClientSecure.h>` is safe — no POSIX build will see it. Change the forward declaration to a full include (needed to use it as a value member, not a pointer).

Add three private members to `Esp32ArduinoTransport`:

```cpp
WiFiClientSecure client_;
std::string caCertStorage_;
bool clientConfigured_ = false;  // true = CA cert loaded; does NOT mean socket is connected
```

`clientConfigured_` tracks only whether `setCACert()` has been called. Socket liveness is a separate concern managed via error handling below.

### Implementation changes

In `post()`, replace the stack-local `WiFiClientSecure client` and `std::string caCertStorage` with a one-time CA setup block:

```cpp
if (!clientConfigured_) {
    if (!configureTlsClient(client_, caCertStorage_)) {
        // emit TLS config error event
        return {kNoStatusCode, TransportError::Tls};
    }
    clientConfigured_ = true;
}
```

Call `http.setReuse(true)` before `http.POST()` to explicitly enable keep-alive and prevent `http.end()` / the `HTTPClient` destructor from closing the underlying socket. Without this the socket may be torn down even with a persistent `WiFiClientSecure`.

`HTTPClient http` stays stack-local — it is a lightweight request wrapper. `http.begin(client_, url)` reuses the underlying socket if it is still connected. If the server has closed the connection, Arduino's `HTTPClient` reconnects on the next `POST()`, paying TLS cost for that one request only.

On transport or network error, stop the socket so the next call gets a clean reconnect rather than reusing a poisoned connection:

```cpp
if (response.error != TransportError::None) {
    client_.stop();
    // clientConfigured_ stays true — CA cert is still valid
}
```

Update `configureTlsClient` to operate on `client_` and `caCertStorage_` directly (remove the out-parameter signature) or keep it and call it with the members.

### Verifying reuse works

The timing log in Step 2 should show the first request in a drain batch paying ~200-500 ms (TLS handshake) and subsequent requests in the same batch paying significantly less (~20-80 ms). If all requests show similar high latency, `setReuse(true)` is not taking effect — Arduino `HTTPClient` behavior here can be version-sensitive. The next experiment in that case is making `HTTPClient http` a persistent member alongside `client_`, but don't start there. Let the timing logs confirm whether `setReuse(true)` with a stack-local `HTTPClient` is sufficient first.

### PosixCurlTransport (desktop / CI)

Same problem: `curl_easy_init()` / `curl_easy_cleanup()` per call. Fix: add a `CURL* curl_` member, initialise in the constructor, clean up in the destructor. In `post()`, re-set all per-request options rather than reinitialising the handle. curl reuses the connection automatically via HTTP keep-alive when the handle is persistent.

Options that must be reset per call: URL, POST body + size, timeout, write callback + buffer, headers (`curl_slist` — free the previous list, build a new one, set it). TLS and CA options only need to be set once.

---

## Step 2: firmware - per-drain timing log

**File:** `src/main.cpp`, `syncOutputs()`

After connection reuse is in place, add elapsed-time logging around `drainPending()`. A single average across the whole drain call is not enough — it hides whether only the first request paid TLS cost. The most useful signal comes from per-request timing inside the transport (an `elapsed_ms` field on the existing `http_post_complete` diagnostic event), but at minimum log total elapsed and average at the `syncOutputs` level:

```cpp
const std::uint64_t drainStart = platform::millis();
const auto drain = app.apiOutboxClient.drainPending(nowMs);
const std::uint64_t drainElapsed = platform::millis() - drainStart;

if (drain.attempted > 0) {
    logLine(LogLevel::Debug,
        "drain: attempted=" + std::to_string(drain.attempted) +
        " sent=" + std::to_string(drain.sent) +
        " elapsed_ms=" + std::to_string(drainElapsed) +
        " avg_ms=" + std::to_string(drainElapsed / drain.attempted));
}
```

If per-request transport events are also emitted with elapsed time, the first-vs-subsequent pattern will be directly visible in the log. Without that, run a drain of 2+ queued items and compare the average against a single-item drain to infer whether subsequent requests are cheaper.

Do not increase `drainRateCap` until per-request timing confirms the connection is being reused and the worst-case failed-send path (which retries synchronously) does not block the loop too long.
