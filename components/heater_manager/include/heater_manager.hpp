#pragma once
#include "zero_cross.hpp"
#include "triac_control.hpp"
#include "heater_controller.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PRODUCTION_TEMPERATURE_C 80.0f

class HeaterManager {
public:
    HeaterManager();
    void init();                                  // call once
    void setTargetTemperature(float target);
    void updateTemperature(float current_temp);   // from the sensor task
    void start();                                 // operator action: clears a latched fault
    void stop();
    bool isRunning();
    float getCurrentPower();
    void setPIDTunings(float kp, float ki, float kd);
    void getPIDTunings(float* kp, float* ki, float* kd);
    const char* faultText();

private:
    static void controlTask(void* arg);
    static void onZeroCross(void* arg);           // runs in the GPIO ISR
    void controlLoop();

    ZeroCrossDetector zero_cross;
    TriacController triac;
    alco::heater::HeaterController controller;    // must be declared after triac
    TaskHandle_t task_{nullptr};
};