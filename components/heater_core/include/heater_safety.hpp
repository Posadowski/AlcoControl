#pragma once
#include <cstdint>
#include <mutex>

namespace alco::heater {

enum class Fault : uint8_t {
    None,
    InvalidReading,   // NaN / inf
    TooLow,           // below minValidC
    Overtemp,         // above maxValidC
    SensorTimeout     // no valid reading for timeoutMs
};

const char* toString(Fault f);

struct SafetyConfig {
    float    minValidC = -20.0f;   // below this the reading is treated as a sensor fault
    float    maxValidC = 120.0f;   // above this: overtemperature cut-off
    uint32_t timeoutMs = 5000;     // max age of the last valid reading
};

// Decides whether heating is allowed. Time is passed in, so it is testable.
// A fault is latched until clear() is called explicitly.
class HeaterSafety {
public:
    explicit HeaterSafety(SafetyConfig cfg = {}) : cfg_(cfg) {}

    void  arm(uint64_t nowMs);                         // call on start()
    void disarm();                                     // stop watching; a latched fault is kept
    void  onTemperature(float celsius, uint64_t nowMs);// readings are ignored while disarmed
    bool  check(uint64_t nowMs);                       // true = heating allowed
    void  clear();                                     // operator reset (also disarms)

    Fault fault() const;
    float lastTemperature() const;

private:
    SafetyConfig cfg_;
    mutable std::mutex m_;
    bool     armed_{false};
    bool     hasReading_{false};
    Fault    fault_{Fault::None};
    float    lastC_{0.f};
    uint64_t lastMs_{0};
};

} // namespace alco::heater