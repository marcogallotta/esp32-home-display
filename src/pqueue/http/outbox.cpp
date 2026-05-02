#include "outbox.h"

#include "request_envelope.h"

namespace pqueue::http {
namespace {

bool isSuccessfulStatus(int statusCode) {
    return statusCode >= 200 && statusCode < 300;
}

bool isRetryableStatus(int statusCode) {
    return statusCode == 408 || statusCode == 429 || (statusCode >= 500 && statusCode < 600);
}

} // namespace

Outbox::Outbox(
    Config httpConfig,
    ClockCallback clock,
    void* clockContext
) : httpConfig_(httpConfig),
    outbox_(httpConfig.queue, httpConfig.outbox, sendStoredRequest, this, clock, clockContext) {}

pqueue::SubmitResult Outbox::submitPost(const std::string& path, const std::string& body) {
    RequestEnvelope request;
    request.method = Method::Post;
    request.path = path;
    request.body = body;

    std::string encoded;
    if (!encodeRequestEnvelope(request, encoded)) {
        return {pqueue::SubmitStatus::SendError};
    }

    return outbox_.submit(encoded);
}

pqueue::DrainResult Outbox::drain() {
    return outbox_.drain();
}

Stats Outbox::stats() {
    return outbox_.stats();
}

SendResult Outbox::sendStoredRequest(void* context, const std::string& encodedRequest, const RetryState& retry) {
    return static_cast<Outbox*>(context)->sendStoredRequest(encodedRequest, retry);
}

SendResult Outbox::sendStoredRequest(const std::string& encodedRequest, const RetryState&) {
    if (httpConfig_.post == nullptr) {
        return {SendDecision::Drop};
    }

    RequestEnvelope request;
    if (!decodeRequestEnvelope(encodedRequest, request) || request.method != Method::Post) {
        return {SendDecision::Drop};
    }

    const std::string url = buildUrl(request.path);
    const Response response = httpConfig_.post(
        httpConfig_.postContext,
        url.c_str(),
        httpConfig_.headers,
        httpConfig_.headerCount,
        reinterpret_cast<const std::uint8_t*>(request.body.data()),
        request.body.size()
    );
    return {classifyResponse(response)};
}

SendDecision Outbox::classifyResponse(const Response& response) const {
    if (httpConfig_.classify != nullptr) {
        return httpConfig_.classify(httpConfig_.classifyContext, response);
    }
    return defaultClassifyResponse(response);
}

std::string Outbox::buildUrl(const std::string& path) const {
    const std::string baseUrl = httpConfig_.baseUrl == nullptr ? std::string{} : std::string{httpConfig_.baseUrl};
    if (baseUrl.empty()) {
        return path;
    }
    if (path.empty()) {
        return baseUrl;
    }

    const bool baseEndsWithSlash = baseUrl.back() == '/';
    const bool pathStartsWithSlash = path.front() == '/';
    if (baseEndsWithSlash && pathStartsWithSlash) {
        return baseUrl + path.substr(1);
    }
    if (!baseEndsWithSlash && !pathStartsWithSlash) {
        return baseUrl + '/' + path;
    }
    return baseUrl + path;
}

SendDecision defaultClassifyResponse(const Response& response) {
    if (response.error != TransportError::None) {
        return SendDecision::RetryLater;
    }
    if (isSuccessfulStatus(response.statusCode)) {
        return SendDecision::Sent;
    }
    if (isRetryableStatus(response.statusCode)) {
        return SendDecision::RetryLater;
    }
    switch (response.statusCode) {
        case 400:
        case 401:
        case 403:
        case 404:
        case 422:
            return SendDecision::Drop;
        default:
            return SendDecision::RetryLater;
    }
}

} // namespace pqueue::http
