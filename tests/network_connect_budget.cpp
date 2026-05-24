#include "doctest/doctest.h"
#include "network_connect_budget.h"

TEST_SUITE("WifiConnectBudget") {

TEST_CASE("not started: remainingMs returns full timeout") {
    network::WifiConnectBudget b;
    CHECK(b.remainingMs(1000, 15000) == 15000);
}

TEST_CASE("kickStart then immediate check: full timeout remains") {
    network::WifiConnectBudget b;
    b.kickStart(1000);
    CHECK(b.remainingMs(1000, 15000) == 15000);
}

TEST_CASE("partial elapsed: remaining is reduced by elapsed time") {
    network::WifiConnectBudget b;
    b.kickStart(1000);
    CHECK(b.remainingMs(3000, 15000) == 13000);
}

TEST_CASE("elapsed equals timeout: remaining is zero") {
    network::WifiConnectBudget b;
    b.kickStart(0);
    CHECK(b.remainingMs(15000, 15000) == 0);
}

TEST_CASE("elapsed exceeds timeout: remaining is zero, not underflow") {
    network::WifiConnectBudget b;
    b.kickStart(0);
    CHECK(b.remainingMs(20000, 15000) == 0);
}

TEST_CASE("clear resets state: remainingMs returns full timeout again") {
    network::WifiConnectBudget b;
    b.kickStart(0);
    b.clear();
    CHECK(b.remainingMs(20000, 15000) == 15000);
}

TEST_CASE("kickStart after clear: new start time is used") {
    network::WifiConnectBudget b;
    b.kickStart(0);
    b.clear();
    b.kickStart(10000);
    CHECK(b.remainingMs(12000, 15000) == 13000);
}

TEST_CASE("uint32_t wraparound: elapsed computed correctly") {
    network::WifiConnectBudget b;
    // startedMs near the uint32_t max, nowMs has wrapped
    const uint32_t start = 0xFFFFFF00u;
    const uint32_t now   = 0x000000FFu; // wrapped by 0x1FF = 511ms
    b.kickStart(start);
    CHECK(b.remainingMs(now, 15000) == 15000 - 511);
}

} // TEST_SUITE
