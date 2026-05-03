#pragma once

#include <cstdint>

#include "status.h"

namespace pqueue {

enum class Severity {
    Debug,
    Info,
    Warning,
    Error,
};

enum class EventKind {
    Diagnostic,
    RequestSent,
    RequestRetried,
    RequestDropped,
};

constexpr std::uint32_t kNoSequence = 0xffffffffU;

struct Event {
    EventKind kind = EventKind::Diagnostic;
    Severity severity = Severity::Debug;
    Status status = Status::success();
    const char* component = "";
    const char* operation = "";
    std::uint32_t sequence = kNoSequence;
    const char* path = "";
    int httpStatus = 0;
    const char* method = "";
};

using EventSink = void (*)(const Event& event, void* user);

struct EventOptions {
    EventSink sink = nullptr;
    void* user = nullptr;

    void emit(const Event& event) const {
        if (sink != nullptr) {
            sink(event, user);
        }
    }
};

} // namespace pqueue
