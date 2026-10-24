#include "pid_controller.hpp"
#include <algorithm>
#include <cmath>

PIDController::PIDController(float kp, float ki, float kd)
: kp_(kp), ki_(ki), kd_(kd), band_(INFINITY) {}

void PIDController::setTarget(float target) { target_ = target; }

bool PIDController::setTunings(float kp, float ki, float kd) {
    if (!std::isfinite(kp) || !std::isfinite(ki) || !std::isfinite(kd)) return false;
    if (kp < 0.0f || ki < 0.0f || kd < 0.0f) return false;
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
    return true;
}

void PIDController::setOutputLimits(float minOut, float maxOut) {
    if (!std::isfinite(minOut) || !std::isfinite(maxOut) || minOut >= maxOut) return;
    outMin_ = minOut;
    outMax_ = maxOut;
    iTerm_ = std::clamp(iTerm_, outMin_, outMax_);
}

void PIDController::setIntegralBand(float band) {
    band_ = (band > 0.0f) ? band : INFINITY;      // NaN/<=0 -> no limit
}

float PIDController::compute(float input, float dt) {
    // Safe output for a bad measurement; the controller state is left untouched.
    if (!std::isfinite(input) || !std::isfinite(target_)) return outMin_;

    const float error = target_ - input;
    const bool  dtOk  = std::isfinite(dt) && dt > 0.0f;

    float derivative = 0.0f;
    if (dtOk && !first_) derivative = -(input - lastInput_) / dt;   // on measurement

    const float p = kp_ * error;
    const float d = kd_ * derivative;

    if (dtOk) {
        const float outNow = p + iTerm_ + d;
        const bool pushingUp   = error > 0.0f && outNow >= outMax_;
        const bool pushingDown = error < 0.0f && outNow <= outMin_;
        if (!pushingUp && !pushingDown && std::fabs(error) <= band_) {
            iTerm_ = std::clamp(iTerm_ + ki_ * error * dt, outMin_, outMax_);
        }
    }

    lastInput_ = input;
    first_ = false;

    const float out = p + iTerm_ + d;
    if (!std::isfinite(out)) return outMin_;
    return std::clamp(out, outMin_, outMax_);
}

void PIDController::reset() {
    iTerm_ = 0.0f;
    lastInput_ = 0.0f;
    first_ = true;
}