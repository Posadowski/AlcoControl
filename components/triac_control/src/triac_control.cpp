#include "triac_control.hpp"
#include "esp_timer.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "TRIAC";

TriacController::TriacController() : current_power_percent(0.0f) {}

void TriacController::init() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TRIAC_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    
    gpio_set_level(TRIAC_PIN, 0);
    ESP_LOGI(TAG, "Triac controller initialized on GPIO %d", TRIAC_PIN);
}

void TriacController::setPowerPercent(float power_percent) {
    if (power_percent < 0.0f) power_percent = 0.0f;
    if (power_percent > 100.0f) power_percent = 100.0f;
    current_power_percent = power_percent;
}

uint32_t TriacController::calculateDelayFromPower(float power_percent) {
    if (power_percent <= 0.0f) return AC_HALF_PERIOD_US;
    if (power_percent >= 100.0f) return 100; // Minimalne opóźnienie
    
    // Konwersja procent mocy na kąt fazowy (0-180°)
    float phase_angle = (1.0f - power_percent / 100.0f) * 180.0f;
    float phase_radians = phase_angle * M_PI / 180.0f;
    
    // Opóźnienie w mikrosekundach
    return (uint32_t)(phase_radians * AC_HALF_PERIOD_US / M_PI);
}

void TriacController::triggerAtDelay(uint32_t delay_us) {
    if (delay_us >= AC_HALF_PERIOD_US) return; // Za późno
    
    esp_rom_delay_us(delay_us);
    gpio_set_level(TRIAC_PIN, 1);
    esp_rom_delay_us(100); // Krótki impuls wyzwalający
    gpio_set_level(TRIAC_PIN, 0);
}

void TriacController::turnOff() {
    current_power_percent = 0.0f;
    gpio_set_level(TRIAC_PIN, 0);
}