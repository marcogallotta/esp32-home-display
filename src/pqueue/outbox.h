#pragma once

#include <cstdint>
#include <string>

#include "queue.h"
#include "types.h"

namespace pqueue {

enum class SendDecision {
    Sent,
    RetryLater,
    Drop,
};

struct RetryState {
    std::uint8_t attempts = 0;
};

struct SendResult {
    SendDecision decision = SendDecision::Drop;
};

// The payload string is treated as opaque bytes, not text. It may contain NULs.
using SendCallback = SendResult (*)(void* context, const std::string& payload, const RetryState& retry);

// Must return monotonic milliseconds. Do not use wall/NTP time here.
// ESP32 callers should use esp_timer_get_time() / 1000.
using ClockCallback = std::uint64_t (*)(void* context);

// Persistent-only v1 outbox config.
// Retry attempt count is persisted in the envelope; retry cooldown timing is RAM-only.
struct OutboxConfig {
    std::uint8_t maxAttempts = 5;
    std::uint16_t maxDrainAttemptsPerSecond = 1;
    std::uint32_t retryDelayMs = 60000;
    // TODO: make CRC configurable once the envelope has a published compatibility policy.
    // TODO: consider burst drain/backoff strategies after the strict FIFO v1 settles.
};

enum class SubmitStatus {
    Sent,
    Queued,
    Dropped,
    QueueFull,
    SendError,
};

struct SubmitResult {
    SubmitStatus status = SubmitStatus::SendError;
};

struct DrainResult {
    std::uint16_t attempts = 0;
    std::uint16_t sent = 0;
    std::uint16_t dropped = 0;
    std::uint16_t droppedMaxAttempts = 0;
    std::uint16_t corruptDropped = 0;
    bool rateLimited = false;
    bool notDue = false;
    bool queueError = false;
    bool sendError = false;
};

// Generic store-and-forward lifecycle over Queue.
// This class is transport-agnostic: no HTTP, JSON, API keys, or app-specific logging.
class Outbox {
public:
    Outbox(
        Queue& queue,
        OutboxConfig config,
        SendCallback send,
        void* sendContext,
        ClockCallback clock,
        void* clockContext
    );

    SubmitResult submit(const std::string& payload);
    DrainResult drain();
    Stats stats();

private:
    SubmitResult enqueueRecord(const std::string& payload, std::uint8_t attempts);
    void setFrontCooldown(std::uint64_t nextAttemptMs);
    void clearFrontCooldown();
    bool frontIsCoolingDown(std::uint64_t nowMs) const;
    bool drainRateAllows(std::uint64_t nowMs) const;
    std::uint64_t drainIntervalMs() const;

    Queue& queue_;
    OutboxConfig config_;
    SendCallback send_ = nullptr;
    void* sendContext_ = nullptr;
    ClockCallback clock_ = nullptr;
    void* clockContext_ = nullptr;
    std::uint64_t lastDrainAttemptMs_ = 0;
    bool hasDrainAttempt_ = false;
    std::uint64_t frontNextAttemptMs_ = 0;
    bool hasFrontCooldown_ = false;
};

} // namespace pqueue
