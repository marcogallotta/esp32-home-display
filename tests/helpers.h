#pragma once

#include "doctest/doctest.h"

#include <string>

template <typename T, typename U>
void assertEqual(const T& actual, const U& expected, const std::string& message) {
    INFO(message);
    CHECK_EQ(actual, expected);
}

inline void assertTrue(bool condition, const std::string& message) {
    INFO(message);
    CHECK(condition);
}

template <typename T>
void assertInRange(const T& actual, const T& min, const T& max, const std::string& message) {
    INFO(message);
    CHECK_GE(actual, min);
    CHECK_LE(actual, max);
}
