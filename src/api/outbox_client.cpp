#include "outbox_client.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#ifndef ARDUINO
#include <filesystem>
#endif

#ifdef ARDUINO
#include <LittleFS.h>
#include "pqueue/http/esp32_arduino_transport.h"
#else
#include "pqueue/http/posix_curl_transport.h"
#endif

#include "pqueue/http/outbox.h"
#include "pqueue/http/request_envelope.h"

#include "../log.h"
#include "../network.h"
#include "../platform.h"
#include "dropped_log.h"
#include "payloads.h"

namespace api {

#ifndef ARDUINO
std::string detail::resolveDesktopPemPathForApiOutbox(const std::string& pemFile) {
    if (pemFile.empty()) {
        return {};
    }

    const std::filesystem::path configured(pemFile);
    if (configured.is_absolute()) {
        return (std::filesystem::path("data") / configured.filename()).string();
    }

    return pemFile;
}
#endif

namespace {

std::string invalidTimestampReason(const std::optional<std::int64_t>& epochS) {
    if (!epochS.has_value()) {
        return "missing timestamp";
    }

    if (*epochS <= 0) {
        return "invalid timestamp " + std::to_string(*epochS);
    }

    return "timestamp invalid";
}

std::string invalidSwitchbotPayloadReason(const SwitchbotReading& reading) {
    if (!reading.temperatureC.has_value()) {
        return "missing temperature";
    }

    if (!reading.humidityPct.has_value()) {
        return "missing humidity";
    }

    if (!reading.lastSeenEpochS.has_value() || *reading.lastSeenEpochS <= 0) {
        return invalidTimestampReason(reading.lastSeenEpochS);
    }

    return "unknown invalid payload";
}

std::string invalidXiaomiPayloadReason(const XiaomiReading& reading) {
    if (!reading.lastSeenEpochS.has_value() || *reading.lastSeenEpochS <= 0) {
        return invalidTimestampReason(reading.lastSeenEpochS);
    }

    if (!reading.temperatureC.has_value() &&
        !reading.moisturePct.has_value() &&
        !reading.lux.has_value() &&
        !reading.conductivityUsCm.has_value()) {
        return "missing all sensor values";
    }

    return "unknown invalid payload";
}

WriteResult makeWriteResult(
    WriteStatus status,
    BackendWriteResult backendResult = BackendWriteResult::Failed,
    int httpStatusCode = 0,
    std::string body = "",
    WriteQueueReason queueReason = WriteQueueReason::None
) {
    WriteResult result;
    result.status = status;
    result.backendResult = backendResult;
    result.httpStatusCode = httpStatusCode;
    result.body = std::move(body);
    result.queueReason = queueReason;
    return result;
}

WriteStatus classifySubmitSendError(pqueue::StatusCode code) {
    switch (code) {
        case pqueue::StatusCode::EncodeFailed:
        case pqueue::StatusCode::InvalidArgument:
        case pqueue::StatusCode::RecordTooLarge:
        case pqueue::StatusCode::Dropped:
            return WriteStatus::DroppedPermanent;

        case pqueue::StatusCode::QueueFull:
            return WriteStatus::DroppedQueueFull;

        case pqueue::StatusCode::Ok:
        case pqueue::StatusCode::BackendUnavailable:
        case pqueue::StatusCode::MountFailed:
        case pqueue::StatusCode::ReadFailed:
        case pqueue::StatusCode::WriteFailed:
        case pqueue::StatusCode::RenameFailed:
        case pqueue::StatusCode::RemoveFailed:
        case pqueue::StatusCode::ListFailed:
        case pqueue::StatusCode::InvalidIndex:
        case pqueue::StatusCode::InvalidRecord:
        case pqueue::StatusCode::CrcMismatch:
        case pqueue::StatusCode::QueueEmpty:
        case pqueue::StatusCode::LockTimeout:
        case pqueue::StatusCode::DecodeFailed:
        case pqueue::StatusCode::SendFailed:
            return WriteStatus::FailedTemporary;
    }

    return WriteStatus::FailedTemporary;
}

std::string diagnosticMac(const ApiRequest& request) {
    const std::string marker = "\"mac\":\"";
    const auto start = request.payload.find(marker);
    if (start == std::string::npos) {
        return {};
    }

    const auto valueStart = start + marker.size();
    const auto valueEnd = request.payload.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return {};
    }

    return request.payload.substr(valueStart, valueEnd - valueStart);
}


LogLevel mapPqueueSeverity(pqueue::Severity severity) {
    switch (severity) {
        case pqueue::Severity::Debug:
            return LogLevel::Debug;
        case pqueue::Severity::Info:
            return LogLevel::Info;
        case pqueue::Severity::Warning:
            return LogLevel::Warn;
        case pqueue::Severity::Error:
            return LogLevel::Error;
    }
    return LogLevel::Error;
}

int logLevelRank(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return 0;
        case LogLevel::Info:
            return 1;
        case LogLevel::Warn:
            return 2;
        case LogLevel::Error:
            return 3;
    }
    return 3;
}

bool pqueueEventEnabled(api::PqueueLogLevel configured, LogLevel eventLevel) {
    switch (configured) {
        case api::PqueueLogLevel::Debug:
            return true;
        case api::PqueueLogLevel::Info:
            return logLevelRank(eventLevel) >= logLevelRank(LogLevel::Info);
        case api::PqueueLogLevel::Warning:
            return logLevelRank(eventLevel) >= logLevelRank(LogLevel::Warn);
        case api::PqueueLogLevel::Error:
            return logLevelRank(eventLevel) >= logLevelRank(LogLevel::Error);
        case api::PqueueLogLevel::None:
            return false;
    }
    return logLevelRank(eventLevel) >= logLevelRank(LogLevel::Info);
}

const char* pqueueEventKindName(pqueue::EventKind kind) {
    switch (kind) {
        case pqueue::EventKind::Diagnostic:
            return "diagnostic";
        case pqueue::EventKind::RequestSent:
            return "sent";
        case pqueue::EventKind::RequestRetried:
            return "retry";
        case pqueue::EventKind::RequestDropped:
            return "dropped";
    }
    return "event";
}

void logPqueueEvent(const pqueue::Event& event, api::PqueueLogLevel configured) {
    // Dropped-request persistence still goes through the existing HTTP onDrop callback.
    // The next patch can move that to RequestDropped events without changing behavior here.
    if (event.kind == pqueue::EventKind::RequestDropped) {
        return;
    }

    const LogLevel level = mapPqueueSeverity(event.severity);
    if (!pqueueEventEnabled(configured, level)) {
        return;
    }

    std::string message = "pqueue ";
    message += pqueueEventKindName(event.kind);
    message += " ";
    message += event.component == nullptr ? "" : event.component;
    if (event.operation != nullptr && event.operation[0] != '\0') {
        message += ".";
        message += event.operation;
    }

    message += ": ";
    message += event.status.message == nullptr || event.status.message[0] == '\0'
        ? pqueue::statusCodeName(event.status.code)
        : event.status.message;

    message += " code=";
    message += pqueue::statusCodeName(event.status.code);

    if (event.status.backendCode != 0) {
        message += " backend=";
        message += std::to_string(event.status.backendCode);
    }
    if (event.queueCount != 0) {
        message += " queued=";
        message += std::to_string(event.queueCount);
    }
    if (event.attempt != 0) {
        message += " attempt=";
        message += std::to_string(event.attempt);
    }
    if (event.remainingMs != 0) {
        message += " remaining_ms=";
        message += std::to_string(event.remainingMs);
    }
    if (event.method != nullptr && event.method[0] != '\0') {
        message += " method=";
        message += event.method;
    }
    if (event.path != nullptr && event.path[0] != '\0') {
        message += " path=";
        message += event.path;
    }
    if (event.httpStatus != 0) {
        message += " http=";
        message += std::to_string(event.httpStatus);
    }
    if (event.bodyBytes != 0) {
        message += " body_bytes=";
        message += std::to_string(event.bodyBytes);
    }
    if (event.timeoutMs != 0) {
        message += " timeout_ms=";
        message += std::to_string(event.timeoutMs);
    }
    if (event.headerCount != 0) {
        message += " headers=";
        message += std::to_string(event.headerCount);
    }

    logLine(level, message);
}

network::TransportResult mapTransportError(pqueue::http::TransportError error) {
    switch (error) {
        case pqueue::http::TransportError::None:
            return network::TransportResult::Ok;
        case pqueue::http::TransportError::Timeout:
            return network::TransportResult::Timeout;
        case pqueue::http::TransportError::Tls:
            return network::TransportResult::TlsError;
        case pqueue::http::TransportError::Network:
        case pqueue::http::TransportError::Dns:
            return network::TransportResult::NetworkError;
        case pqueue::http::TransportError::Unknown:
            return network::TransportResult::InternalError;
    }
    return network::TransportResult::InternalError;
}

network::HttpResponse toNetworkResponse(const pqueue::http::Response& response) {
    network::HttpResponse out;
    out.transport = mapTransportError(response.error);
    out.statusCode = response.statusCode;
    out.body = response.body;
    return out;
}

std::string dropReasonName(pqueue::http::DropReason reason) {
    switch (reason) {
        case pqueue::http::DropReason::DecodeFailed:
            return "decode_failed";
        case pqueue::http::DropReason::ClassifiedDrop:
            return "classified_drop";
    }
    return "dropped";
}

void logDroppedQueueFullRequest(const ApiRequest& request) {
    const std::string mac = diagnosticMac(request);
    logLine(
        LogLevel::Warn,
        "Dropping request because pqueue queue is full: " + request.path +
        (mac.empty() ? std::string{} : " for " + mac)
    );

    dropped_log::appendDroppedRequest(
        "queue_full",
        request.path,
        mac,
        request.payload,
        0,
        -1,
        "",
        0,
        0
    );
}

std::string defaultQueueBasePath() {
#ifdef ARDUINO
    return "/pqueue_api_spool";
#else
    return "pqueue_api_spool";
#endif
}

pqueue::http::Config makeHttpConfig(
    const ::Config& config,
    const std::array<pqueue::http::Header, 2>& headers,
    void* callbackContext,
    pqueue::http::ResponseCallback onResponse,
    pqueue::http::DropCallback onDrop,
    pqueue::EventSink onEvent,
    const std::string& queueBasePath
) {
    pqueue::http::Config httpConfig;
    httpConfig.queue.basePath = queueBasePath;
    httpConfig.queue.reservedBytes = config.api.outbox.diskReserveBytes;
    httpConfig.queue.events = {onEvent, callbackContext};
    httpConfig.outbox.retryDelayMs = config.api.outbox.retryDelayMs;
    httpConfig.outbox.events = {onEvent, callbackContext};
    httpConfig.outbox.maxDrainAttemptsPerSecond = config.api.outbox.drainRateCap <= 0
        ? 1
        : static_cast<std::uint16_t>(config.api.outbox.drainRateCap);
    httpConfig.baseUrl = config.api.baseUrl.c_str();
    httpConfig.headers = headers.data();
    httpConfig.headerCount = headers.size();
    httpConfig.responseContext = callbackContext;
    httpConfig.onResponse = onResponse;
    httpConfig.dropContext = callbackContext;
    httpConfig.onDrop = onDrop;
    return httpConfig;
}

#ifdef ARDUINO
bool networkReadyCallback(void* context, std::uint32_t timeoutMs) {
    const auto* config = static_cast<const ::Config*>(context);
    return network::platform(config->wifi).networkReady(timeoutMs);
}
#endif

} // namespace

struct OutboxClientImpl {
    explicit OutboxClientImpl(const ::Config& config)
        : config(config),
          headers{{
              {"Content-Type", "application/json"},
              {"x-api-key", config.api.apiKey.c_str()},
          }},
          ownedTransport(std::make_unique<TransportType>(makeTransportConfig(config, {&OutboxClientImpl::onPqueueEvent, this}))),
          transport(ownedTransport.get()),
          outbox(
              makeHttpConfig(config, headers, this, &OutboxClientImpl::onResponse, &OutboxClientImpl::onDrop, &OutboxClientImpl::onPqueueEvent, defaultQueueBasePath()),
              *transport,
              &OutboxClientImpl::clockNow,
              nullptr
          ) {}

    OutboxClientImpl(
        const ::Config& config,
        pqueue::http::Transport& injectedTransport,
        std::string queueBasePath,
        pqueue::ClockCallback clock,
        void* clockContext
    )
        : config(config),
          headers{{
              {"Content-Type", "application/json"},
              {"x-api-key", config.api.apiKey.c_str()},
          }},
          transport(&injectedTransport),
          outbox(
              makeHttpConfig(config, headers, this, &OutboxClientImpl::onResponse, &OutboxClientImpl::onDrop, &OutboxClientImpl::onPqueueEvent, queueBasePath),
              *transport,
              clock,
              clockContext
          ) {}

#ifdef ARDUINO
    static pqueue::http::Esp32ArduinoTransportConfig makeTransportConfig(const ::Config& config, pqueue::EventOptions events) {
        pqueue::http::Esp32ArduinoTransportConfig transportConfig;
        transportConfig.common.userAgent = "my-app/1.0";
        transportConfig.common.timeoutMs = 15000;
        transportConfig.common.events = events;
        transportConfig.caCertPath = config.api.pemFile.empty() ? nullptr : config.api.pemFile.c_str();
        transportConfig.caCertFileSystem = &LittleFS;
        transportConfig.networkReadyContext = const_cast<::Config*>(&config);
        transportConfig.networkReady = &networkReadyCallback;
        return transportConfig;
    }

    using TransportType = pqueue::http::Esp32ArduinoTransport;
#else
    static pqueue::http::PosixCurlTransportConfig makeTransportConfig(const ::Config& config, pqueue::EventOptions events) {
        pqueue::http::PosixCurlTransportConfig transportConfig;
        transportConfig.common.userAgent = "my-app/1.0";
        transportConfig.common.timeoutMs = 15000;
        transportConfig.common.events = events;
        transportConfig.caBundlePath = detail::resolveDesktopPemPathForApiOutbox(config.api.pemFile);
        return transportConfig;
    }

    using TransportType = pqueue::http::PosixCurlTransport;
#endif


    static std::uint64_t clockNow(void* context) {
        (void)context;
        return platform::millis();
    }

    static void onResponse(
        void* context,
        const pqueue::http::RequestEnvelope& request,
        const pqueue::http::Response& response
    ) {
        (void)request;
        auto* self = static_cast<OutboxClientImpl*>(context);
        self->lastResponse = response;
    }

    static void onDrop(
        void* context,
        const pqueue::http::RequestEnvelope* request,
        pqueue::http::DropReason reason,
        const pqueue::http::Response* response
    ) {
        auto* self = static_cast<OutboxClientImpl*>(context);
        self->logDrop(request, reason, response);
    }

    static void onPqueueEvent(const pqueue::Event& event, void* context) {
        const auto* self = static_cast<const OutboxClientImpl*>(context);
        logPqueueEvent(
            event,
            self == nullptr ? api::PqueueLogLevel::Info : self->config.api.outbox.logLevel
        );
    }

    void logDrop(
        const pqueue::http::RequestEnvelope* request,
        pqueue::http::DropReason reason,
        const pqueue::http::Response* response
    ) const {
        const std::string path = request == nullptr ? std::string{} : request->path;
        const std::string body = request == nullptr ? std::string{} : request->body;
        const ApiRequest apiRequest{path, body};
        const std::string mac = diagnosticMac(apiRequest);
        const network::HttpResponse networkResponse = response == nullptr
            ? network::HttpResponse{}
            : toNetworkResponse(*response);

        logLine(
            LogLevel::Warn,
            "Dropping API request from pqueue: " + dropReasonName(reason) +
            (path.empty() ? std::string{} : ", " + path) +
            (mac.empty() ? std::string{} : " for " + mac) +
            ", HTTP " + std::to_string(networkResponse.statusCode)
        );

        dropped_log::appendDroppedRequest(
            dropReasonName(reason),
            path,
            mac,
            body,
            networkResponse.statusCode,
            static_cast<int>(networkResponse.transport),
            networkResponse.error,
            0,
            0
        );
    }

    const ::Config& config;
    std::array<pqueue::http::Header, 2> headers;
    std::unique_ptr<pqueue::http::Transport> ownedTransport;
    pqueue::http::Transport* transport = nullptr;
    pqueue::http::Outbox outbox;
    std::optional<pqueue::http::Response> lastResponse;
};

OutboxClient::OutboxClient(const ::Config& config)
    : config_(config),
      pqueue_(std::make_unique<OutboxClientImpl>(config_)) {}

#ifndef ARDUINO
OutboxClient::OutboxClient(
    const ::Config& config,
    pqueue::http::Transport& transport,
    std::string queueBasePath,
    pqueue::ClockCallback clock,
    void* clockContext
)
    : config_(config),
      pqueue_(std::make_unique<OutboxClientImpl>(config_, transport, std::move(queueBasePath), clock, clockContext)) {}
#endif

OutboxClient::~OutboxClient() = default;

WriteResult OutboxClient::postSwitchbotReading(
    const SensorIdentity& identity,
    const SwitchbotReading& reading
) {
    const auto payload = makeSwitchbotPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(
            LogLevel::Warn,
            "Dropping SwitchBot reading: invalid payload for " + identity.mac +
            ": " + invalidSwitchbotPayloadReason(reading)
        );
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return send(ApiRequest{
        "/switchbot/reading",
        toJson(*payload),
    });
}

WriteResult OutboxClient::postXiaomiReading(
    const SensorIdentity& identity,
    const XiaomiReading& reading
) {
    const auto payload = makeXiaomiPayload(identity, reading);
    if (!payload.has_value()) {
        logLine(
            LogLevel::Warn,
            "Dropping Xiaomi reading: invalid payload for " + identity.mac +
            ": " + invalidXiaomiPayloadReason(reading)
        );
        return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return send(ApiRequest{
        "/xiaomi/reading",
        toJson(*payload),
    });
}

WriteResult OutboxClient::send(ApiRequest request) {
    pqueue_->lastResponse.reset();
    const pqueue::SubmitResult submitResult =
        pqueue_->outbox.submitPost(request.path, request.payload);

    const network::HttpResponse response = pqueue_->lastResponse.has_value()
        ? toNetworkResponse(*pqueue_->lastResponse)
        : network::HttpResponse{};

    switch (submitResult.status) {
        case pqueue::SubmitStatus::Sent:
            return makeWriteResult(
                WriteStatus::Sent,
                parseBackendWriteResult(response),
                response.statusCode,
                response.body
            );

        case pqueue::SubmitStatus::Queued:
            logLine(
                LogLevel::Debug,
                "Queued API request in pqueue: " +
                std::to_string(pqueue_->outbox.stats().count) + " queued"
            );
            return makeWriteResult(
                WriteStatus::Queued,
                BackendWriteResult::Failed,
                response.statusCode,
                response.body,
                pqueue_->outbox.stats().count > 1 ? WriteQueueReason::BacklogPresent : WriteQueueReason::RetryableFailure
            );

        case pqueue::SubmitStatus::Dropped:
            return makeWriteResult(
                WriteStatus::DroppedPermanent,
                BackendWriteResult::Failed,
                response.statusCode,
                response.body
            );

        case pqueue::SubmitStatus::QueueFull:
            logDroppedQueueFullRequest(request);
            return makeWriteResult(WriteStatus::DroppedQueueFull);

        case pqueue::SubmitStatus::SendError:
            return makeWriteResult(
                classifySubmitSendError(submitResult.detail.code),
                BackendWriteResult::Failed,
                response.statusCode,
                response.body
            );
    }

    return makeWriteResult(WriteStatus::FailedTemporary);
}

OutboxDrainResult OutboxClient::drainPending(std::uint64_t nowMs) {
    (void)nowMs;
    OutboxDrainResult result;
    const std::uint16_t maxDrain = config_.api.outbox.drainRateCap <= 0
        ? 1
        : static_cast<std::uint16_t>(config_.api.outbox.drainRateCap);
    const pqueue::DrainResult drainResult = pqueue_->outbox.drainUpTo(maxDrain);
    result.attempted = drainResult.attempts;
    result.sent = drainResult.sent;
    result.dropped = drainResult.dropped + drainResult.corruptDropped;
    result.notDueYet = drainResult.notDue || drainResult.rateLimited;
    result.blockedByRetryableFailure = drainResult.sendError || drainResult.queueError;
    return result;
}

} // namespace api
