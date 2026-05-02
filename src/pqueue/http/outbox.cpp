#include "outbox.h"

#include "request_envelope.h"

namespace pqueue::http {
namespace {

bool isSuccessfulStatus(int statusCode) {
    return statusCode >= 200 && statusCode < 300;
}

bool isRetryableStatus(int statusCode) {
    return statusCode == 408 ||
           statusCode == 429 ||
           statusCode == 500 ||
           statusCode == 502 ||
           statusCode == 503 ||
           statusCode == 504;
}

} // namespace

CallbackTransport::CallbackTransport(PostCallback post, void* context)
    : post_(post), context_(context) {}

Response CallbackTransport::post(
    const char* url,
    const Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
) {
    if (post_ == nullptr) {
        return {kNoStatusCode, TransportError::Unknown};
    }
    return post_(context_, url, headers, headerCount, body, bodySize);
}

Outbox::Outbox(
    Config httpConfig,
    Transport& transport,
    ClockCallback clock,
    void* clockContext
) : httpConfig_(httpConfig),
    transport_(transport),
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
    RequestEnvelope request;
    if (!decodeRequestEnvelope(encodedRequest, request) || request.method != Method::Post) {
        return {SendDecision::Drop};
    }

    const std::string url = buildUrl(request.path);
    const Response response = transport_.post(
        url.c_str(),
        httpConfig_.headers,
        httpConfig_.headerCount,
        reinterpret_cast<const std::uint8_t*>(request.body.data()),
        request.body.size()
    );
    notifyResponse(request, response);
    return {classifyResponse(response)};
}

void Outbox::notifyResponse(const RequestEnvelope& request, const Response& response) const {
    if (httpConfig_.onResponse != nullptr) {
        httpConfig_.onResponse(httpConfig_.responseContext, request, response);
    }
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

    // TODO: consider a slow-retry policy for capacity-style responses such as 507.
    return SendDecision::Drop;
}

} // namespace pqueue::http
