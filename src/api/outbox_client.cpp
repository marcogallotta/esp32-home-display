#include "outbox_client.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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

void onPqueueEvent(const pqueue::Event& event, void*) {
    if (event.kind != pqueue::EventKind::Diagnostic) {
        // Drop/request lifecycle events are still handled by the existing HTTP onDrop/onResponse callbacks.
        return;
    }

    std::string message = "pqueue ";
    message += event.component == nullptr ? "" : event.component;
    if (event.operation != nullptr && event.operation[0] != '\0') {
        message += ".";
        message += event.operation;
    }
    message += ": ";
    message += event.status.message == nullptr ? "" : event.status.message;
    if (event.path != nullptr && event.path[0] != '\0') {
        message += " [";
        message += event.path;
        message += "]";
    }

    logLine(mapPqueueSeverity(event.severity), message);
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
        case pqueue::http::DropReason::MaxAttempts:
            return "max_attempts";
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

pqueue::http::Config makeHttpConfig(
    const ::Config& config,
    const std::array<pqueue::http::Header, 2>& headers,
    void* callbackContext,
    pqueue::http::ResponseCallback onResponse,
    pqueue::http::DropCallback onDrop,
    pqueue::EventSink onEvent
) {
    pqueue::http::Config httpConfig;
#ifdef ARDUINO
    httpConfig.queue.basePath = "/pqueue_api_spool";
#else
    httpConfig.queue.basePath = "pqueue_api_spool";
#endif
    httpConfig.queue.diskReserveBytes = config.api.outbox.diskReserveBytes;
    httpConfig.queue.events = {onEvent, callbackContext};
    httpConfig.outbox.retryDelayMs = static_cast<std::uint32_t>(config.api.outbox.drainRateTickS) * 1000U;
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
          transport(makeTransportConfig(config)),
          outbox(
              makeHttpConfig(config, headers, this, &OutboxClientImpl::onResponse, &OutboxClientImpl::onDrop, &onPqueueEvent),
              transport,
              &OutboxClientImpl::clockNow,
              this
          ) {}

#ifdef ARDUINO
    static pqueue::http::Esp32ArduinoTransportConfig makeTransportConfig(const ::Config& config) {
        pqueue::http::Esp32ArduinoTransportConfig transportConfig;
        transportConfig.common.userAgent = "my-app/1.0";
        transportConfig.common.timeoutMs = 15000;
        transportConfig.caCertPath = config.api.pemFile.empty() ? nullptr : config.api.pemFile.c_str();
        transportConfig.caCertFileSystem = &LittleFS;
        transportConfig.networkReadyContext = const_cast<::Config*>(&config);
        transportConfig.networkReady = &networkReadyCallback;
        return transportConfig;
    }

    using TransportType = pqueue::http::Esp32ArduinoTransport;
#else
    static pqueue::http::PosixCurlTransportConfig makeTransportConfig(const ::Config& config) {
        pqueue::http::PosixCurlTransportConfig transportConfig;
        transportConfig.common.userAgent = "my-app/1.0";
        transportConfig.common.timeoutMs = 15000;
        transportConfig.caBundlePath = config.api.pemFile.empty() ? nullptr : config.api.pemFile.c_str();
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
    TransportType transport;
    pqueue::http::Outbox outbox;
    std::optional<pqueue::http::Response> lastResponse;
};

OutboxClient::OutboxClient(const ::Config& config)
    : config_(config),
      pqueue_(std::make_unique<OutboxClientImpl>(config_)) {}

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
                LogLevel::Warn,
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
            return makeWriteResult(WriteStatus::DroppedPermanent);
    }

    return makeWriteResult(WriteStatus::DroppedPermanent);
}

OutboxDrainResult OutboxClient::drainPending(std::uint64_t nowMs) {
    (void)nowMs;
    OutboxDrainResult result;
    const pqueue::DrainResult drainResult = pqueue_->outbox.drain();
    result.attempted = drainResult.attempts;
    result.sent = drainResult.sent;
    result.dropped = drainResult.dropped +
        drainResult.droppedMaxAttempts +
        drainResult.corruptDropped;
    result.notDueYet = drainResult.notDue || drainResult.rateLimited;
    result.blockedByRetryableFailure = drainResult.sendError || drainResult.queueError;
    return result;
}

} // namespace api
