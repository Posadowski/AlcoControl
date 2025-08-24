#pragma once

class PIDController {
public:
    PIDController(float kp, float ki, float kd);
    void setTarget(float target);
    void setTunings(float kp, float ki, float kd);
    float compute(float input, float dt);
    void reset();
    
private:
    float kp, ki, kd;
    float target;
    float integral;
    float last_error;
    bool first_run;
};