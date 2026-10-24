#pragma once

// PI(D) controller for a heater: output is a power in percent (default 0..100).
//  - the integral term is stored in OUTPUT units (ki is applied while integrating),
//    so changing ki at run time does not make the output jump
//  - anti-windup: the integral term is clamped to the output range and is frozen
//    while the output is saturated in the direction the error pushes
//  - optional integral band: integrate only when |error| <= band.
//    CAUTION: the band must be larger than (holding power / kp), otherwise the
//    loop settles outside the band and the integral never engages. Off by default.
//  - derivative acts on the measurement, so a setpoint change gives no kick
class PIDController {
public:
    PIDController(float kp, float ki, float kd);

    void  setTarget(float target);
    float target() const { return target_; }

    bool  setTunings(float kp, float ki, float kd);      // false (and no change) if invalid
    void  setOutputLimits(float minOut, float maxOut);   // default 0..100
    void  setIntegralBand(float band);                   // default: no limit

    float compute(float input, float dt);                // dt in seconds
    void  reset();

    float integralTerm() const { return iTerm_; }        // in output units, for diagnostics

private:
    float kp_, ki_, kd_;
    float target_{0.0f};
    float outMin_{0.0f};
    float outMax_{100.0f};
    float band_;
    float iTerm_{0.0f};
    float lastInput_{0.0f};
    bool  first_{true};
};