#ifndef ARDUINO

#include "network.h"

#include <chrono>
#include <iostream>

#include <curl/curl.h>

namespace forecast {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
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

    HttpResponse httpGet(const std::string& url, const std::string& pem) override {
        HttpResponse resp;

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.error = "curl_easy_init failed";
            return resp;
        }

        curl_blob caBlob{};
        caBlob.data = const_cast<char*>(pem.data());
        caBlob.len = pem.size();
        caBlob.flags = CURL_BLOB_COPY;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "my-app/1.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &caBlob);

        const CURLcode rc = curl_easy_perform(curl);
        if (rc != CURLE_OK) {
            resp.error = curl_easy_strerror(rc);
            curl_easy_cleanup(curl);
            return resp;
        }

        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.status_code = static_cast<int>(code);

        curl_easy_cleanup(curl);
        return resp;
    }

private:
    std::chrono::steady_clock::time_point start_;
};

Platform& platform(const WifiConfig& wifiConfig) {
    static DesktopPlatform instance(wifiConfig);
    return instance;
}

} // namespace forecast

#endif