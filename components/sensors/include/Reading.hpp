#pragma once
#include <cstdint>

namespace alco::core {

struct Timestamp {
    uint64_t ms; // uptime
};

struct Reading {
    float value;        // np. temperature, humidity, etc.
    Timestamp ts;
};

} // namespace