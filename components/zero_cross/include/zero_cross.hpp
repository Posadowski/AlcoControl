#pragma once
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ZERO_CROSS_PIN GPIO_NUM_4  // Dostosuj do swojego układu

class ZeroCrossDetector {
public:
    ZeroCrossDetector();
    void init();
    bool waitForZeroCross(uint32_t timeout_ms = 10);
    uint32_t getLastZeroCrossTime();
    
private:
    static void IRAM_ATTR zeroCrossISR(void* arg);
    QueueHandle_t zero_cross_queue;
    uint32_t last_zero_cross_time;
};