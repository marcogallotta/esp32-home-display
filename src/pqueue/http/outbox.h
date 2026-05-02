#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "pqueue/outbox.h"


namespace pqueue::http {

constexpr int kNoStatusCode = 0;

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
    int statusCode = kNoStatusCode;
    TransportError error = TransportError::Unknown;
};

struct TransportConfig {
    std::uint32_t timeoutMs = 15000;
    const char* userAgent = "pqueue-http/1.0";
    bool followRedirects = true;
    bool allowInsecureTls = false;
};

using PostCallback = Response (*)(
    void* context,
    const char* url,
    const Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
);

class Transport {
public:
    virtual ~Transport() = default;

    // TODO: add a generic send() API if/when GET or other methods are supported.
    virtual Response post(
        const char* url,
        const Header* headers,
        std::size_t headerCount,
        const std::uint8_t* body,
        std::size_t bodySize
    ) = 0;
};

class CallbackTransport final : public Transport {
public:
    CallbackTransport(PostCallback post, void* context);

    Response post(
        const char* url,
        const Header* headers,
        std::size_t headerCount,
        const std::uint8_t* body,
        std::size_t bodySize
    ) override;

private:
    PostCallback post_ = nullptr;
    void* context_ = nullptr;
};

using ClassifyResponseCallback = SendDecision (*)(void* context, const Response& response);

struct Config {
    pqueue::Config queue;
    pqueue::OutboxConfig outbox;

    const char* baseUrl = "";
    const Header* headers = nullptr;
    std::size_t headerCount = 0;

    void* classifyContext = nullptr;
    ClassifyResponseCallback classify = nullptr;
};

class Outbox {
public:
    // TODO: add an advanced constructor for dependency-injected core Outbox/storage in tests or custom backends.
    Outbox(
        Config httpConfig,
        Transport& transport,
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
    Transport& transport_;
    pqueue::Outbox outbox_;
};

SendDecision defaultClassifyResponse(const Response& response);

} // namespace pqueue::http
