#include "posix_curl_transport.h"

#include <curl/curl.h>

#include <string>

namespace pqueue::http {
namespace {

void ensureCurlGlobalInit() {
    static const CURLcode initResult = curl_global_init(CURL_GLOBAL_DEFAULT);
    (void)initResult;
}

std::size_t discardResponseBody(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

TransportError mapCurlError(CURLcode code) {
    switch (code) {
        case CURLE_OK:
            return TransportError::None;
        case CURLE_OPERATION_TIMEDOUT:
            return TransportError::Timeout;
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            return TransportError::Dns;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_ENGINE_NOTFOUND:
        case CURLE_SSL_ENGINE_SETFAILED:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_SHUTDOWN_FAILED:
        case CURLE_SSL_CRL_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
            return TransportError::Tls;
        case CURLE_COULDNT_CONNECT:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
            return TransportError::Network;
        default:
            return TransportError::Unknown;
    }
}

curl_slist* buildCurlHeaders(const Header* headers, std::size_t headerCount) {
    curl_slist* list = nullptr;
    for (std::size_t i = 0; i < headerCount; ++i) {
        if (headers[i].name == nullptr || headers[i].value == nullptr) {
            continue;
        }
        std::string header = std::string(headers[i].name) + ": " + headers[i].value;
        curl_slist* next = curl_slist_append(list, header.c_str());
        if (next == nullptr) {
            curl_slist_free_all(list);
            return nullptr;
        }
        list = next;
    }
    return list;
}

} // namespace

PosixCurlTransport::PosixCurlTransport(PosixCurlTransportConfig config)
    : config_(config) {
    ensureCurlGlobalInit();
}

Response PosixCurlTransport::post(
    const char* url,
    const Header* headers,
    std::size_t headerCount,
    const std::uint8_t* body,
    std::size_t bodySize
) {
    if (url == nullptr) {
        return {kNoStatusCode, TransportError::Unknown};
    }

    ensureCurlGlobalInit();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return {kNoStatusCode, TransportError::Unknown};
    }

    curl_slist* curlHeaders = buildCurlHeaders(headers, headerCount);
    if (headerCount > 0 && curlHeaders == nullptr) {
        curl_easy_cleanup(curl);
        return {kNoStatusCode, TransportError::Unknown};
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char*>(body));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodySize));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.common.timeoutMs));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discardResponseBody);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, config_.common.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, config_.common.allowInsecureTls ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, config_.common.allowInsecureTls ? 0L : 2L);

    if (config_.common.userAgent != nullptr) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.common.userAgent);
    }
    if (config_.caBundlePath != nullptr) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, config_.caBundlePath);
    }
    if (curlHeaders != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);
    }

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = kNoStatusCode;
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    }

    curl_slist_free_all(curlHeaders);
    curl_easy_cleanup(curl);

    return {static_cast<int>(statusCode), mapCurlError(result)};
}

} // namespace pqueue::http
