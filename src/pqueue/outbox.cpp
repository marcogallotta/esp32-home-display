#include "outbox.h"

#include <limits>

#include "envelope.h"

namespace pqueue {

Outbox::Outbox(
    Config queueConfig,
    OutboxConfig config,
    SendCallback send,
    void* sendContext,
    ClockCallback clock,
    void* clockContext
) : queue_(queueConfig),
    config_(config),
    send_(send),
    sendContext_(sendContext),
    clock_(clock),
    clockContext_(clockContext) {}

SubmitResult Outbox::submit(const std::string& payload) {
    if (send_ == nullptr || clock_ == nullptr || config_.maxAttempts == 0) {
        return {SubmitStatus::SendError};
    }

    if (queue_.stats().count > 0) {
        return enqueueRecord(payload, 0);
    }

    const SendResult result = send_(sendContext_, payload, RetryState{});
    switch (result.decision) {
        case SendDecision::Sent:
            return {SubmitStatus::Sent};
        case SendDecision::Drop:
            return {SubmitStatus::Dropped};
        case SendDecision::RetryLater: {
            const auto queued = enqueueRecord(payload, 1);
            if (queued.status == SubmitStatus::Queued) {
                setFrontCooldown(clock_(clockContext_) + config_.retryDelayMs);
            }
            return queued;
        }
    }

    return {SubmitStatus::SendError};
}

DrainResult Outbox::drain() {
    DrainResult result;
    if (send_ == nullptr || clock_ == nullptr || config_.maxAttempts == 0) {
        result.sendError = true;
        return result;
    }

    const std::uint64_t nowMs = clock_(clockContext_);
    std::string record;
    if (!queue_.peek(record)) {
        clearFrontCooldown();
        return result;
    }

    envelope::DecodedEnvelope decoded;
    if (!envelope::decodeEnvelope(record, decoded)) {
        if (!queue_.pop()) {
            result.queueError = true;
            return result;
        }
        clearFrontCooldown();
        result.corruptDropped += 1;
        return result;
    }

    if (frontIsCoolingDown(nowMs)) {
        result.notDue = true;
        return result;
    }

    if (!drainRateAllows(nowMs)) {
        result.rateLimited = true;
        return result;
    }
    lastDrainAttemptMs_ = nowMs;
    hasDrainAttempt_ = true;

    result.attempts += 1;
    const SendResult sendResult = send_(sendContext_, decoded.payload, RetryState{decoded.attempts});
    switch (sendResult.decision) {
        case SendDecision::Sent:
            if (!queue_.pop()) {
                result.queueError = true;
                return result;
            }
            clearFrontCooldown();
            result.sent += 1;
            return result;

        case SendDecision::Drop:
            if (!queue_.pop()) {
                result.queueError = true;
                return result;
            }
            clearFrontCooldown();
            result.dropped += 1;
            return result;

        case SendDecision::RetryLater: {
            if (decoded.attempts == std::numeric_limits<std::uint8_t>::max()) {
                if (!queue_.pop()) {
                    result.queueError = true;
                    return result;
                }
                clearFrontCooldown();
                result.droppedMaxAttempts += 1;
                return result;
            }

            const std::uint8_t nextAttempts = static_cast<std::uint8_t>(decoded.attempts + 1);
            if (nextAttempts >= config_.maxAttempts) {
                if (!queue_.pop()) {
                    result.queueError = true;
                    return result;
                }
                clearFrontCooldown();
                result.droppedMaxAttempts += 1;
                return result;
            }

            if (!envelope::encodeEnvelope(nextAttempts, decoded.payload, record) || !queue_.rewriteFront(record)) {
                result.queueError = true;
                return result;
            }
            setFrontCooldown(nowMs + config_.retryDelayMs);
            return result;
        }
    }

    result.sendError = true;
    return result;
}

Stats Outbox::stats() {
    return queue_.stats();
}

SubmitResult Outbox::enqueueRecord(const std::string& payload, std::uint8_t attempts) {
    if (attempts >= config_.maxAttempts) {
        return {SubmitStatus::Dropped};
    }

    std::string record;
    if (!envelope::encodeEnvelope(attempts, payload, record)) {
        return {SubmitStatus::SendError};
    }
    return queue_.enqueue(record) ? SubmitResult{SubmitStatus::Queued} : SubmitResult{SubmitStatus::QueueFull};
}

void Outbox::setFrontCooldown(std::uint64_t nextAttemptMs) {
    frontNextAttemptMs_ = nextAttemptMs;
    hasFrontCooldown_ = true;
}

void Outbox::clearFrontCooldown() {
    frontNextAttemptMs_ = 0;
    hasFrontCooldown_ = false;
}

bool Outbox::frontIsCoolingDown(std::uint64_t nowMs) const {
    return hasFrontCooldown_ && frontNextAttemptMs_ > nowMs;
}

bool Outbox::drainRateAllows(std::uint64_t nowMs) const {
    if (!hasDrainAttempt_) {
        return true;
    }
    return nowMs - lastDrainAttemptMs_ >= drainIntervalMs();
}

std::uint64_t Outbox::drainIntervalMs() const {
    const std::uint16_t attemptsPerSecond = config_.maxDrainAttemptsPerSecond == 0
        ? 1
        : config_.maxDrainAttemptsPerSecond;
    const std::uint64_t interval = 1000U / attemptsPerSecond;
    return interval == 0 ? 1 : interval;
}

} // namespace pqueue
