#pragma once

namespace pqueue {

enum class StatusCode {
    Ok,

    InvalidArgument,
    BackendUnavailable,

    MountFailed,
    ReadFailed,
    WriteFailed,
    RenameFailed,
    RemoveFailed,
    ListFailed,

    InvalidIndex,
    InvalidRecord,
    CrcMismatch,
    RecordTooLarge,
    QueueFull,
    QueueEmpty,

    EncodeFailed,
    DecodeFailed,
    SendFailed,
    Dropped,
};

struct Status {
    StatusCode code = StatusCode::Ok;
    int backendCode = 0;
    const char* message = "";

    bool ok() const { return code == StatusCode::Ok; }

    static Status success() { return {}; }
    static Status failure(StatusCode code, const char* message, int backendCode = 0) {
        return {code, backendCode, message};
    }
};

} // namespace pqueue
