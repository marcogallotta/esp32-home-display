#pragma once

#include <stdexcept>
#include <string>

inline void fail(const std::string& message) {
    throw std::runtime_error(message);
}

inline void assertTrue(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

template <typename T, typename U>
inline void assertEqual(const T& actual, const U& expected, const std::string& message) {
    if (!(actual == expected)) {
        fail(message);
    }
}

inline void assertInRange(int value, int lowInclusive, int highInclusive, const std::string& message) {
    if (value < lowInclusive || value > highInclusive) {
        fail(message);
    }
}
