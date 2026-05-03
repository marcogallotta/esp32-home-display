#include "outbox.h"

#include <limits>

#include "envelope.h"

namespace pqueue {
namespace {

SubmitResult submitResult(SubmitStatus submitStatus, Status detail = Status::success()) {
    return {submitStatus, detail};
}

} // namespace

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

void Outbox::emit(Event event) const {
    config_.events.emit(event);
}

void Outbox::emitDiagnostic(Severity severity, Status status, const char* operation) const {
    emit(Event{
        EventKind::Diagnostic,
        severity,
        status,
        "Outbox",
        operation,
    });
}

void Outbox::emitRequestEvent(
    EventKind kind,
    Severity severity,
    Status status,
    const char* operation,
    std::uint8_t attempt,
    std::uint32_t remainingMs
) {
    const Stats currentStats = queue_.stats();
    Event event;
    event.kind = kind;
    event.severity = severity;
    event.status = status;
    event.component = "Outbox";
    event.operation = operation;
    event.attempt = attempt;
    event.queueCount = currentStats.count;
    event.remainingMs = remainingMs;
    emit(event);
}

SubmitResult Outbox::submit(const std::string& payload) {
    if (send_ == nullptr || clock_ == nullptr) {
        const Status st = Status::failure(StatusCode::SendFailed, "outbox is not configured for sending");
        emitDiagnostic(Severity::Error, st, "submit");
        return submitResult(SubmitStatus::SendError, st);
    }

    if (queue_.stats().count > 0) {
        return enqueueRecord(payload, 0);
    }

    const SendResult result = send_(sendContext_, payload, RetryState{});
    switch (result.decision) {
        case SendDecision::Sent:
            emitRequestEvent(EventKind::RequestSent, Severity::Info, Status::success(), "submit", 0, 0);
            return submitResult(SubmitStatus::Sent);
        case SendDecision::Drop: {
            const Status st = Status::failure(StatusCode::Dropped, "request was dropped by send policy");
            emitRequestEvent(EventKind::RequestDropped, Severity::Warning, st, "submit", 0, 0);
            return submitResult(SubmitStatus::Dropped, st);
        }
        case SendDecision::RetryLater: {
            const auto queued = enqueueRecord(payload, 1);
            if (queued.status == SubmitStatus::Queued) {
                setFrontCooldown(clock_(clockContext_) + config_.retryDelayMs);
                emitRequestEvent(EventKind::RequestRetried, Severity::Info, Status::success(), "submit", 1, config_.retryDelayMs);
            }
            return queued;
        }
    }

    const Status st = Status::failure(StatusCode::SendFailed, "unknown send decision");
    emitDiagnostic(Severity::Error, st, "submit");
    return submitResult(SubmitStatus::SendError, st);
}

DrainResult Outbox::drain() {
    return drainOne(true);
}

DrainResult Outbox::drainBurst(std::uint16_t maxAttempts) {
    DrainResult total;
    if (maxAttempts == 0) {
        maxAttempts = 1;
    }

    for (std::uint16_t i = 0; i < maxAttempts; ++i) {
        const DrainResult current = drainOne(false);
        total.attempts += current.attempts;
        total.sent += current.sent;
        total.dropped += current.dropped;
        total.droppedMaxAttempts += current.droppedMaxAttempts;
        total.corruptDropped += current.corruptDropped;
        total.rateLimited = total.rateLimited || current.rateLimited;
        total.notDue = total.notDue || current.notDue;
        total.queueError = total.queueError || current.queueError;
        total.sendError = total.sendError || current.sendError;
        if (!current.detail.ok()) {
            total.detail = current.detail;
        }

        const bool madeProgress = current.sent != 0 || current.dropped != 0 || current.droppedMaxAttempts != 0 || current.corruptDropped != 0;
        const bool shouldStop = current.attempts == 0 || current.notDue || current.rateLimited || current.queueError || current.sendError || !madeProgress;
        if (shouldStop) {
            break;
        }
    }

    return total;
}

DrainResult Outbox::drainOne(bool enforceRateLimit) {
    DrainResult result;
    if (send_ == nullptr || clock_ == nullptr) {
        result.sendError = true;
        result.detail = Status::failure(StatusCode::SendFailed, "outbox is not configured for sending");
        emitDiagnostic(Severity::Error, result.detail, "drain");
        return result;
    }

    const std::uint64_t nowMs = clock_(clockContext_);
    std::string record;
    Status st = queue_.peek(record);
    if (!st.ok()) {
        if (st.code == StatusCode::QueueEmpty) {
            clearFrontCooldown();
            return result;
        }
        result.queueError = true;
        result.detail = st;
        emitDiagnostic(Severity::Error, st, "drain");
        return result;
    }

    envelope::DecodedEnvelope decoded;
    if (!envelope::decodeEnvelope(record, decoded)) {
        st = queue_.pop();
        if (!st.ok()) {
            result.queueError = true;
            result.detail = st;
            emitDiagnostic(Severity::Error, st, "drain");
            return result;
        }
        clearFrontCooldown();
        result.corruptDropped += 1;
        emitRequestEvent(
            EventKind::RequestDropped,
            Severity::Error,
            Status::failure(StatusCode::DecodeFailed, "stored outbox envelope could not be decoded"),
            "drain",
            0,
            0);
        return result;
    }

    if (frontIsCoolingDown(nowMs)) {
        result.notDue = true;
        emitRequestEvent(
            EventKind::Diagnostic,
            Severity::Debug,
            Status::failure(StatusCode::SendFailed, "front request retry cooldown not due"),
            "drain",
            decoded.attempts,
            static_cast<std::uint32_t>(frontNextAttemptMs_ - nowMs));
        return result;
    }

    if (enforceRateLimit && !drainRateAllows(nowMs)) {
        result.rateLimited = true;
        emitRequestEvent(
            EventKind::Diagnostic,
            Severity::Debug,
            Status::failure(StatusCode::SendFailed, "drain rate limit not due"),
            "drain",
            decoded.attempts,
            static_cast<std::uint32_t>(drainIntervalMs() - (nowMs - lastDrainAttemptMs_)));
        return result;
    }
    lastDrainAttemptMs_ = nowMs;
    hasDrainAttempt_ = true;

    result.attempts += 1;
    emitRequestEvent(
        EventKind::Diagnostic,
        Severity::Debug,
        Status::success(),
        "drain_send_start",
        decoded.attempts,
        0);
    const SendResult sendResult = send_(sendContext_, decoded.payload, RetryState{decoded.attempts});
    switch (sendResult.decision) {
        case SendDecision::Sent:
            st = queue_.pop();
            if (!st.ok()) {
                result.queueError = true;
                result.detail = st;
                emitDiagnostic(Severity::Error, st, "drain");
                return result;
            }
            clearFrontCooldown();
            result.sent += 1;
            emitRequestEvent(EventKind::RequestSent, Severity::Info, Status::success(), "drain", decoded.attempts, 0);
            return result;

        case SendDecision::Drop:
            st = queue_.pop();
            if (!st.ok()) {
                result.queueError = true;
                result.detail = st;
                emitDiagnostic(Severity::Error, st, "drain");
                return result;
            }
            clearFrontCooldown();
            result.dropped += 1;
            emitRequestEvent(
                EventKind::RequestDropped,
                Severity::Warning,
                Status::failure(StatusCode::Dropped, "request was dropped by send policy"),
                "drain",
                decoded.attempts,
                0);
            return result;

        case SendDecision::RetryLater: {
            const std::uint8_t nextAttempts = decoded.attempts == std::numeric_limits<std::uint8_t>::max()
                ? decoded.attempts
                : static_cast<std::uint8_t>(decoded.attempts + 1);

            if (!envelope::encodeEnvelope(nextAttempts, decoded.payload, record)) {
                result.queueError = true;
                result.detail = Status::failure(StatusCode::EncodeFailed, "failed to encode retry envelope");
                emitDiagnostic(Severity::Error, result.detail, "drain");
                return result;
            }
            st = queue_.rewriteFront(record);
            if (!st.ok()) {
                result.queueError = true;
                result.detail = st;
                emitDiagnostic(Severity::Error, st, "drain");
                return result;
            }
            setFrontCooldown(nowMs + config_.retryDelayMs);
            emitRequestEvent(EventKind::RequestRetried, Severity::Info, Status::success(), "drain", nextAttempts, config_.retryDelayMs);
            return result;
        }
    }

    result.sendError = true;
    result.detail = Status::failure(StatusCode::SendFailed, "unknown send decision");
    emitDiagnostic(Severity::Error, result.detail, "drain");
    return result;
}

Stats Outbox::stats() {
    return queue_.stats();
}

SubmitResult Outbox::enqueueRecord(const std::string& payload, std::uint8_t attempts) {
    std::string record;
    if (!envelope::encodeEnvelope(attempts, payload, record)) {
        const Status st = Status::failure(StatusCode::EncodeFailed, "failed to encode outbox envelope");
        emitDiagnostic(Severity::Error, st, "enqueueRecord");
        return submitResult(SubmitStatus::SendError, st);
    }
    Status st = queue_.enqueue(record);
    if (st.ok()) {
        return submitResult(SubmitStatus::Queued);
    }
    if (st.code == StatusCode::QueueFull) {
        return submitResult(SubmitStatus::QueueFull, st);
    }
    return submitResult(SubmitStatus::SendError, st);
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
