#include "triac_control.hpp"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char* TAG = "TRIAC";
static constexpr uint32_t kGatePulseUs = 100;

void TriacController::init() {
    gpio_config_t io = {};
    io.intr_type = GPIO_INTR_DISABLE;
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << TRIAC_PIN);
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io);
    gpio_set_level(TRIAC_PIN, 0);

    if (!task_) {
        xTaskCreatePinnedToCore(&TriacController::fireTask, "triac_fire", 2048, this,
                                configMAX_PRIORITIES - 2, &task_, 1);
    }
    ESP_LOGI(TAG, "Triac controller initialized on GPIO %d", (int)TRIAC_PIN);
}

void TriacController::fire() {
    if (!task_) return;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(task_, &woken);
    portYIELD_FROM_ISR(woken);
}

void TriacController::off() {
    gpio_set_level(TRIAC_PIN, 0);
}

void TriacController::fireTask(void* arg) {
    auto* self = static_cast<TriacController*>(arg);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const uint32_t d = self->fireDelayUs_;
        if (d) esp_rom_delay_us(d);
        gpio_set_level(TRIAC_PIN, 1);
        esp_rom_delay_us(kGatePulseUs);
        gpio_set_level(TRIAC_PIN, 0);
    }
}