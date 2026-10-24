#include "zero_cross.hpp"
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "ZERO_CROSS";
static constexpr uint32_t kMinGapUs = 5000;   // crossings are 10 ms apart at 50 Hz; reject glitches

void IRAM_ATTR ZeroCrossDetector::isr(void* arg) {
    auto* self = static_cast<ZeroCrossDetector*>(arg);
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    if (now - self->lastUs_.load(std::memory_order_relaxed) < kMinGapUs) return;
    self->lastUs_.store(now, std::memory_order_relaxed);
    if (self->cb_) self->cb_(self->cbArg_);
}

void ZeroCrossDetector::init(Callback cb, void* arg) {
    cb_ = cb;
    cbArg_ = arg;

    gpio_config_t io = {};
    io.intr_type = GPIO_INTR_NEGEDGE;        // or POSEDGE, depends on the detector circuit
    io.mode = GPIO_MODE_INPUT;
    io.pin_bit_mask = (1ULL << ZERO_CROSS_PIN);
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    const esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   // INVALID_STATE: already installed
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %d", (int)err);
        return;
    }
    gpio_isr_handler_add(ZERO_CROSS_PIN, &ZeroCrossDetector::isr, this);
}