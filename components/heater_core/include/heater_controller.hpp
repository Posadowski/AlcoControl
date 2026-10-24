#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include "burst_scheduler.hpp"
#include "heater_hw.hpp"
#include "heater_safety.hpp"
#include "pid_controller.hpp"

namespace alco::heater {

class HeaterController {
public:
    HeaterController(ITriac& triac, float kp, float ki, float kd,
                     SafetyConfig safety = {},
                     uint32_t windowHalfCycles = 100,
                     uint32_t watchdogHalfCycles = 300);

    // Operator actions. start() clears a latched fault.
    void start(uint64_t nowMs);
    void stop();
    bool running() const { return running_; }

    bool  setTarget(float celsius);              // false if rejected
    float target() const { return target_; }
    bool  setTunings(float kp, float ki, float kd);   // false if rejected
    void  getTunings(float& kp, float& ki, float& kd) const;

    void onTemperature(float celsius, uint64_t nowMs);   // sensor task
    void step(uint64_t nowMs, float dtSec);              // control task, ~1 s
    void onHalfCycle();                                  // zero-cross ISR

    float power() const { return power_; }
    float temperature() const { return temperature_; }
    Fault fault() const { return safety_.fault(); }

private:
    void applyPower(float percent);
    void trip();                                  // immediate cut-off

    ITriac&        triac_;
    BurstScheduler scheduler_;
    HeaterSafety   safety_;
    PIDController  pid_;
    SafetyConfig   cfg_;
    float          kp_, ki_, kd_;                 // guarded by pidMtx_
    uint32_t       watchdog_;
    mutable std::mutex pidMtx_;

    std::atomic<bool>     running_{false};
    std::atomic<float>    power_{0.f};
    std::atomic<float>    target_{0.f};
    std::atomic<float>    temperature_{0.f};
    std::atomic<uint32_t> sinceStep_{0};
};

} // namespace alco::heater