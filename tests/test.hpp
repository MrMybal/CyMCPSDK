#pragma once
#include <cymcp/core.hpp>
#include <iostream>
inline int checks = 0;
inline void check(bool ok, const char* label) {
    ++checks;
    if (!ok)
        throw std::runtime_error(label);
}
template <class F> void expect_error(const std::string& code, F fn) {
    ++checks;
    try {
        fn();
    } catch (const cymcp::Error& e) {
        if (e.code == code)
            return;
        throw std::runtime_error("Expected " + code + ", got " + e.code + ": " + e.what());
    }
    throw std::runtime_error("Expected error " + code);
}
