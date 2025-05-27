#ifndef INC_PID_H_
#define INC_PID_H_

typedef struct {
    float Kp;
    float Ki;
    float Kd;

    float setpoint;     // desired value (for example 78.5°C)

    float integral;
    float last_error;

    float output_min;   // minimum output value
    float output_max;   // maximum output value
} PID_t;

void PID_Init(PID_t *pid, float Kp, float Ki, float Kd, float output_min, float output_max);
float PID_Compute(PID_t *pid, float measured_value, float dt);

#endif /* INC_PID_H_ */
