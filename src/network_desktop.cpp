#ifndef ARDUINO

#include "network.h"

#include <chrono>
#include <iostream>

#include <curl/curl.h>

namespace network {
namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

TransportResult mapCurlCode(CURLcode rc) {
    switch (rc) {
        case CURLE_OK:
            return TransportResult::Ok;

        case CURLE_OPERATION_TIMEDOUT:
        // Handle the newer constant only if it exists
        #ifdef CURLE_CONNECT_TIMEOUT
        case CURLE_CONNECT_TIMEOUT:
        #endif
            return TransportResult::Timeout;

        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_CACERT_BADFILE:
            return TransportResult::TlsError;

        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
            return TransportResult::NetworkError;

        case CURLE_FAILED_INIT:
        case CURLE_URL_MALFORMAT:
        case CURLE_NOT_BUILT_IN:
        case CURLE_OUT_OF_MEMORY:
            return TransportResult::InternalError;

        default:
            return TransportResult::NetworkError;
    }
}

class DesktopPlatform final : public Platform {
public:
    explicit DesktopPlatform(const WifiConfig& wifiConfig)
        : Platform(wifiConfig),
          start_(std::chrono::steady_clock::now()) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~DesktopPlatform() override {
        curl_global_cleanup();
    }

    void log(const std::string& msg) override {
        std::cerr << msg << '\n';
    }

    uint64_t millis() const override {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

    bool networkReady(unsigned long timeoutMs = 15000) override {
        (void)timeoutMs;
        return true;
    }

    HttpResponse request(const Request& request) override {
        return performRequest(request);
    }

private:
    HttpResponse performRequest(const Request& request) {
        HttpResponse resp;

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.transport = TransportResult::InternalError;
            return resp;
        }

        curl_slist* curlHeaders = nullptr;

        if (request.method == Method::Post) {
            curlHeaders = curl_slist_append(
                curlHeaders,
                ("Content-Type: " + request.contentType).c_str()
            );
        }

        for (const auto& [key, value] : request.headers) {
            curlHeaders = curl_slist_append(curlHeaders, (key + ": " + value).c_str());
        }

        if (curlHeaders != nullptr) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curlHeaders);
        }

        if (request.method == Method::Post) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }

        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "my-app/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

        if (request.pem.empty()) {
            log("Warning: empty PEM for " + request.url + "; TLS verification disabled");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        } else {
            curl_blob caBlob{};
            caBlob.data = const_cast<char*>(request.pem.data());
            caBlob.len = request.pem.size();
            caBlob.flags = CURL_BLOB_COPY;

            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &caBlob);
        }

        const CURLcode rc = curl_easy_perform(curl);
        resp.transport = mapCurlCode(rc);

        if (rc != CURLE_OK) {
            resp.error = curl_easy_strerror(rc);
            if (curlHeaders != nullptr) {
                curl_slist_free_all(curlHeaders);
            }
            curl_easy_cleanup(curl);
            return resp;
        }

        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);

        if (curlHeaders != nullptr) {
            curl_slist_free_all(curlHeaders);
        }
        curl_easy_cleanup(curl);
        return resp;
    }

    std::chrono::steady_clock::time_point start_;
};

} // namespace

Platform& platform(const WifiConfig& wifiConfig) {
    static DesktopPlatform instance(wifiConfig);
    return instance;
}

} // namespace network

#endif
