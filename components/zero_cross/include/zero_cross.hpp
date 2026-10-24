#pragma once
#include <atomic>
#include <cstdint>
#include "driver/gpio.h"
#include "esp_attr.h"

#define ZERO_CROSS_PIN GPIO_NUM_27   // CHECK: must not be the DS18B20 pin

class ZeroCrossDetector {
public:
    using Callback = void (*)(void* arg);   // called from the GPIO ISR, once per half-cycle

    void init(Callback cb, void* arg);
    uint32_t lastCrossUs() const { return lastUs_; }

private:
    static void IRAM_ATTR isr(void* arg);

    Callback cb_{nullptr};
    void* cbArg_{nullptr};
    std::atomic<uint32_t> lastUs_{0};
};