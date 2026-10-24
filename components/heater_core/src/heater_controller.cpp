#include "heater_controller.hpp"
#include <cmath>

using namespace alco::heater;

HeaterController::HeaterController(ITriac& triac, float kp, float ki, float kd,
                                   SafetyConfig safety, uint32_t windowHalfCycles,
                                   uint32_t watchdogHalfCycles)
: triac_(triac),
  scheduler_(windowHalfCycles),
  safety_(safety),
  pid_(kp, ki, kd),
  cfg_(safety),
  kp_(kp), ki_(ki), kd_(kd),
  watchdog_(watchdogHalfCycles) {}

void HeaterController::start(uint64_t nowMs) {
    {
        std::lock_guard<std::mutex> lk(pidMtx_);
        pid_.reset();
        pid_.setTarget(target_);
    }
    scheduler_.reset();
    applyPower(0.0f);
    safety_.clear();
    safety_.arm(nowMs);
    sinceStep_ = 0;
    running_ = true;                 // last: the ISR path starts acting only now
}

void HeaterController::stop() {
    running_ = false;
    applyPower(0.0f);
    triac_.off();
    safety_.disarm();                // a latched fault stays visible
}

bool HeaterController::setTarget(float c) {
    if (!std::isfinite(c) || c < cfg_.minValidC || c > cfg_.maxValidC) return false;
    std::lock_guard<std::mutex> lk(pidMtx_);
    pid_.setTarget(c);
    target_ = c;
    return true;
}

bool HeaterController::setTunings(float kp, float ki, float kd) {
    std::lock_guard<std::mutex> lk(pidMtx_);
    if (!pid_.setTunings(kp, ki, kd)) return false;
    kp_ = kp; ki_ = ki; kd_ = kd;
    return true;
}

void HeaterController::getTunings(float& kp, float& ki, float& kd) const {
    std::lock_guard<std::mutex> lk(pidMtx_);
    kp = kp_; ki = ki_; kd = kd_;
}

void HeaterController::onTemperature(float c, uint64_t nowMs) {
    temperature_ = c;                // always kept for display
    if (!running_) return;
    safety_.onTemperature(c, nowMs);
    if (safety_.fault() != Fault::None) trip();      // do not wait for the next step()
}

void HeaterController::step(uint64_t nowMs, float dtSec) {
    sinceStep_ = 0;
    if (!running_) { applyPower(0.0f); return; }

    if (!safety_.check(nowMs)) {
        applyPower(0.0f);
        if (safety_.fault() != Fault::None) trip();
        return;                      // otherwise: waiting for the first reading
    }

    float out;
    {
        std::lock_guard<std::mutex> lk(pidMtx_);
        out = pid_.compute(safety_.lastTemperature(), dtSec);
    }
    applyPower(out);
}

// ISR context: atomics only.
void HeaterController::onHalfCycle() {
    if (!running_.load(std::memory_order_relaxed)) return;

    // Control watchdog: step() is not being called, so the power value is stale.
    const uint32_t n = sinceStep_.load(std::memory_order_relaxed);
    if (n >= watchdog_) return;
    sinceStep_.store(n + 1, std::memory_order_relaxed);

    if (scheduler_.nextHalfCycle()) triac_.fire();
}

void HeaterController::applyPower(float percent) {
    scheduler_.setPower(percent);
    power_ = scheduler_.power();
}

void HeaterController::trip() {
    applyPower(0.0f);
    running_ = false;
    triac_.off();
    safety_.disarm();
}