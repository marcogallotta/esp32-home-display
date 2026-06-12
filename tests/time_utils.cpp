#include "time_utils.h"

#include "doctest/doctest.h"

using time_utils::utcBrokenDownToEpoch;

TEST_CASE("utcBrokenDownToEpoch") {
    SUBCASE("Unix epoch") {
        CHECK(utcBrokenDownToEpoch(1970, 1, 1, 0, 0, 0) == 0);
    }

    SUBCASE("one second past epoch") {
        CHECK(utcBrokenDownToEpoch(1970, 1, 1, 0, 0, 1) == 1);
    }

    SUBCASE("gigasecond 2001-09-09T01:46:40") {
        CHECK(utcBrokenDownToEpoch(2001, 9, 9, 1, 46, 40) == 1000000000LL);
    }

    SUBCASE("2009-02-13T23:31:30") {
        CHECK(utcBrokenDownToEpoch(2009, 2, 13, 23, 31, 30) == 1234567890LL);
    }

    SUBCASE("leap day 2000-02-29T00:00:00") {
        CHECK(utcBrokenDownToEpoch(2000, 2, 29, 0, 0, 0) == 951782400LL);
    }

    SUBCASE("2024-03-10T15:30:45") {
        CHECK(utcBrokenDownToEpoch(2024, 3, 10, 15, 30, 45) == 1710084645LL);
    }

    SUBCASE("pre-epoch returns -1") {
        CHECK(utcBrokenDownToEpoch(1969, 12, 31, 23, 59, 59) == -1);
    }
}
