#pragma once

#include "doctest/doctest.h"

#include <string>

template <typename T, typename U>
void assertEqual(const T& actual, const U& expected, const std::string& message) {
    CHECK_MESSAGE(actual == expected, message);
}

inline void assertTrue(bool condition, const std::string& message) {
    CHECK_MESSAGE(condition, message);
}

template <typename T>
void assertInRange(const T& actual, const T& min, const T& max, const std::string& message) {
    const bool ok = actual >= min && actual <= max;
    CHECK_MESSAGE(ok, message);
}
