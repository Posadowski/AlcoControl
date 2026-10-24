#pragma once
#include <atomic>
#include <cstdint>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heater_hw.hpp"

#define TRIAC_PIN GPIO_NUM_26   // CHECK: GPIO2 is a strapping pin, prefer another one

class TriacController : public alco::heater::ITriac {
public:
    void init();
    void fire() override;     // ISR-safe: wakes the gate task
    void off() override;
    void setFireDelayUs(uint32_t us) { fireDelayUs_ = us; }   // delay after the zero-cross edge

private:
    static void fireTask(void* arg);

    TaskHandle_t task_{nullptr};
    std::atomic<uint32_t> fireDelayUs_{300};
};