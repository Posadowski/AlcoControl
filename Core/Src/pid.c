#include "pid.h"

void PID_Init(PID_t *pid, float Kp, float Ki, float Kd, float output_min, float output_max)
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->setpoint = 0.0f;
}

float PID_Compute(PID_t *pid, float measured_value, float dt)
{
    float error = pid->setpoint - measured_value;

    pid->integral += error * dt;

    // (anti-windup)
    if (pid->integral > 100.0f) pid->integral = 100.0f;
    if (pid->integral < -100.0f) pid->integral = -100.0f;

    float derivative = (error - pid->last_error) / dt;

    float output = pid->Kp * error + pid->Ki * pid->integral + pid->Kd * derivative;

    // Output restriction
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    pid->last_error = error;

    return output;
}
