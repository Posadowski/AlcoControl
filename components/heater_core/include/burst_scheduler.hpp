#pragma once
#include <atomic>
#include <cstdint>

namespace alco::heater {

// Integral-cycle ("burst fire") power control.
// Call nextHalfCycle() once per AC half-cycle (at every zero crossing);
// true means "fire the triac in this half-cycle".
// Integer-only, no allocation, no blocking, so it is safe to call from an ISR.
class BurstScheduler {
public:
    explicit BurstScheduler(uint32_t windowHalfCycles = 100);

    void  setPower(float percent);   // clamped to 0..100, NaN -> 0
    float power() const;             // effective power (after rounding)
    bool  nextHalfCycle();
    void  reset();

private:
    uint32_t window_;
    std::atomic<uint32_t> onCount_{0};
    std::atomic<uint32_t> acc_{0};               // touched only by nextHalfCycle()/reset()
};

} // namespace alco::heater