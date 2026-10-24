#include "burst_scheduler.hpp"

using namespace alco::heater;

BurstScheduler::BurstScheduler(uint32_t windowHalfCycles)
: window_(windowHalfCycles ? windowHalfCycles : 1) {}

void BurstScheduler::setPower(float p) {
    if (!(p > 0.0f)) { onCount_ = 0; return; }          // <= 0 and NaN
    if (p >= 100.0f) { onCount_ = window_; return; }
    onCount_ = static_cast<uint32_t>(p * window_ / 100.0f + 0.5f);
}

float BurstScheduler::power() const {
    return 100.0f * static_cast<float>(onCount_.load()) / static_cast<float>(window_);
}

// Bresenham-style accumulator: spreads onCount fires evenly over window_ calls.
bool BurstScheduler::nextHalfCycle() {
    uint32_t a = acc_.load(std::memory_order_relaxed) + onCount_.load(std::memory_order_relaxed);
    const bool fire = a >= window_;
    if (fire) a -= window_;
    acc_.store(a, std::memory_order_relaxed);
    return fire;
}

void BurstScheduler::reset() { 
    acc_.store(0, std::memory_order_relaxed); 
}