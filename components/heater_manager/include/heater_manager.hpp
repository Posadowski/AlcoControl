#pragma once
#include "zero_cross.hpp"
#include "triac_control.hpp"
#include "pid_controller.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PRODUCTION_TEMPERATURE_C 80.0f

class HeaterManager {
public:
    HeaterManager();
    void init();
    void setTargetTemperature(float target);
    void updateTemperature(float current_temp);
    void start();
    void stop();
    bool isRunning();
    float getCurrentPower();
    void setPIDTunings(float kp, float ki, float kd);
    void getPIDTunings(float* kp, float* ki, float* kd);
    
private:
    // Komponenty w kolejności inicjalizacji
    ZeroCrossDetector zero_cross;
    TriacController triac;
    PIDController pid;
    
    // Dane stanu
    float current_temperature;
    float target_temperature;
    float current_power;
    bool running;
    TaskHandle_t control_task_handle;
    
    // Metody prywatne
    static void controlTask(void* pvParameter);
    void controlLoop();
    void controlZeroCross();
    uint32_t calculateDelayFromPower(float power_percent);
};