#include "api/buffer.h"
#include "api/request_file_store.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <string>

namespace {

void cleanSpool() {
#ifndef ARDUINO
    std::error_code ec;
    std::filesystem::remove_all("spool", ec);
    std::filesystem::create_directory("spool", ec);
#endif
}

api::BufferedRequest makeRequest(std::string body) {
    api::BufferedRequest request;
    request.path = "/switchbot/reading";
    request.mac = "AA:BB:CC:DD:EE:FF";
    request.body = std::move(body);
    request.timeoutRetryCount = 2;
    request.tlsRetryCount = 1;
    return request;
}

} // namespace

TEST_CASE("request file store round-trips request fields and hydrates mac from body") {
    cleanSpool();

    const auto request = makeRequest(
        "{\"mac\":\"EC:2E:84:06:4E:9A\",\"name\":\"Bed\",\"type\":\"switchbot\"}"
    );
    REQUIRE(api::request_file_store::writeRequest(23, request));

    api::BufferedRequest loaded;
    REQUIRE(api::request_file_store::readRequest(23, loaded));

    CHECK_EQ(loaded.path, request.path);
    CHECK_EQ(loaded.body, request.body);
    CHECK_EQ(loaded.mac, "EC:2E:84:06:4E:9A");
    CHECK_EQ(loaded.timeoutRetryCount, 2);
    CHECK_EQ(loaded.tlsRetryCount, 1);
}

TEST_CASE("request file store derives mac from persisted body not stale request metadata") {
    cleanSpool();

    auto request = makeRequest(
        "{\"mac\":\"11:22:33:44:55:66\",\"name\":\"Bed\",\"type\":\"switchbot\"}"
    );
    request.mac = "AA:BB:CC:DD:EE:FF";
    REQUIRE(api::request_file_store::writeRequest(24, request));

    api::BufferedRequest loaded;
    REQUIRE(api::request_file_store::readRequest(24, loaded));

    CHECK_EQ(loaded.mac, "11:22:33:44:55:66");
    CHECK_EQ(loaded.body, request.body);
}

TEST_CASE("request file store leaves mac empty when body has no mac") {
    cleanSpool();

    const auto request = makeRequest("{\"name\":\"test\"}");
    REQUIRE(api::request_file_store::writeRequest(25, request));

    api::BufferedRequest loaded;
    REQUIRE(api::request_file_store::readRequest(25, loaded));

    CHECK(loaded.mac.empty());
    CHECK_EQ(loaded.body, request.body);
}

TEST_CASE("request file store leaves mac empty when body is not JSON") {
    cleanSpool();

    const auto request = makeRequest("not-json");
    REQUIRE(api::request_file_store::writeRequest(26, request));

    api::BufferedRequest loaded;
    REQUIRE(api::request_file_store::readRequest(26, loaded));

    CHECK(loaded.mac.empty());
    CHECK_EQ(loaded.body, request.body);
}
