#pragma once
#include "driver/gpio.h"

#define TRIAC_PIN GPIO_NUM_2  // Dostosuj do swojego układu
#define AC_PERIOD_US 20000    // 50Hz = 20ms period
#define AC_HALF_PERIOD_US 10000 // 10ms half period

class TriacController {
public:
    TriacController();
    void init();
    void setPowerPercent(float power_percent); // 0-100%
    void triggerAtDelay(uint32_t delay_us);
    void turnOff();
    
private:
    float current_power_percent;
    uint32_t calculateDelayFromPower(float power_percent);
};