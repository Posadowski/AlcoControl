#include "zero_cross.hpp"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "ZERO_CROSS";
static QueueHandle_t s_zero_cross_queue = nullptr;

ZeroCrossDetector::ZeroCrossDetector() {
    zero_cross_queue = xQueueCreate(10, sizeof(uint32_t));
    s_zero_cross_queue = zero_cross_queue;
}

void IRAM_ATTR ZeroCrossDetector::zeroCrossISR(void* arg) {
    uint32_t time = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_zero_cross_queue, &time, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void ZeroCrossDetector::init() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Lub POSEDGE - zależy od układu
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ZERO_CROSS_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    
    gpio_install_isr_service(0);
    gpio_isr_handler_add(ZERO_CROSS_PIN, zeroCrossISR, nullptr);
    
    ESP_LOGI(TAG, "Zero cross detector initialized on GPIO %d", ZERO_CROSS_PIN);
}

bool ZeroCrossDetector::waitForZeroCross(uint32_t timeout_ms) {
    uint32_t time;
    if (xQueueReceive(zero_cross_queue, &time, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        last_zero_cross_time = time;
        return true;
    }
    return false;
}

uint32_t ZeroCrossDetector::getLastZeroCrossTime() {
    return last_zero_cross_time;
}