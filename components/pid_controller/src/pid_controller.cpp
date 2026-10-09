#include "pid_controller.hpp"
#include <algorithm>
#include <cmath>

PIDController::PIDController(float kp, float ki, float kd) 
    : kp(kp), ki(ki), kd(kd), target(0), integral(0), last_error(0), first_run(true) {}

void PIDController::setTarget(float target) {
    this->target = target;
}

void PIDController::setTunings(float kp, float ki, float kd) {
    this->kp = kp;
    this->ki = ki;
    this->kd = kd;
}

float PIDController::compute(float input, float dt) {
    float error = target - input;
    const bool dt_valid = dt > 0.0f;
    
    if (dt_valid) {
        integral += error * dt;
        // Anti-windup
        integral = std::max(-100.0f, std::min(100.0f, integral));
    }

    float derivative = 0.0f;
    if (!first_run) {
        derivative = (error - last_error) / dt;
    }
    
    float output = kp * error + ki * integral + kd * derivative;
    
    last_error = error;
    first_run = false;
    
    // Fail-safe: NaN/inf (e.g., erroneous sensor reading) = heater switched off
    if (!std::isfinite(output)) return 0.0f;

    // Limit the output to 0–100%
    return std::max(0.0f, std::min(100.0f, output));
}

void PIDController::reset() {
    integral = 0;
    last_error = 0;
    first_run = true;
}