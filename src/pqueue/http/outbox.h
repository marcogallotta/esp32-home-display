#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "pqueue/outbox.h"


namespace pqueue::http {

struct Header {
    const char* name = nullptr;
    const char* value = nullptr;
};

enum class TransportError {
    None,
    Timeout,
    Network,
    Tls,
    Dns,
    Unknown,
};

struct Response {
    int statusCode = 0;
    TransportError error = TransportError::Unknown;
};

using PostCallback = Response (*)(
    void* context,
    const char* url,
    const Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
);

using ClassifyResponseCallback = SendDecision (*)(void* context, const Response& response);

struct Config {
    pqueue::Config queue;
    pqueue::OutboxConfig outbox;

    const char* baseUrl = "";
    const Header* headers = nullptr;
    std::size_t headerCount = 0;

    void* postContext = nullptr;
    PostCallback post = nullptr;

    void* classifyContext = nullptr;
    ClassifyResponseCallback classify = nullptr;
};

class Outbox {
public:
    // TODO: add an advanced constructor for dependency-injected core Outbox/storage in tests or custom backends.
    Outbox(
        Config httpConfig,
        ClockCallback clock,
        void* clockContext
    );

    pqueue::SubmitResult submitPost(const std::string& path, const std::string& body);
    pqueue::DrainResult drain();
    Stats stats();

private:
    static SendResult sendStoredRequest(void* context, const std::string& encodedRequest, const RetryState& retry);
    SendResult sendStoredRequest(const std::string& encodedRequest, const RetryState& retry);
    SendDecision classifyResponse(const Response& response) const;
    std::string buildUrl(const std::string& path) const;

    Config httpConfig_;
    pqueue::Outbox outbox_;
};

SendDecision defaultClassifyResponse(const Response& response);

} // namespace pqueue::http
